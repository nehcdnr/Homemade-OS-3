#include"file.h"
#include"interrupt/handler.h"
#include"interrupt/systemcall.h"
#include"memory/memory.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"
#include"io/io.h"
#include<blob.h>

typedef struct OpenFileRequest OpenFileRequest;
struct OpenFileRequest{
	IORequest ior;
	int isReady;
	int isClosing;
	uintptr_t lastReturnValue;

	const BLOBAddress *file;
	uintptr_t offset;
	uintptr_t handle;
	Task *task;
	OpenFileRequest *next, **prev;
};


static OpenFileRequest *openFileList = NULL;
static Spinlock openFileLock = INITIAL_SPINLOCK;

static void *searchOpenFileList(uintptr_t handle, Task *task){
	OpenFileRequest *ofr;
	acquireLock(&openFileLock);
	for(ofr = openFileList; ofr != NULL; ofr = ofr->next){
		if(ofr->handle == handle)
			break;
	}
	releaseLock(&openFileLock);
	if(ofr == NULL)
		return NULL;
	if(ofr->task != task)
		return NULL;
	if(ofr->isReady == 0)
		return NULL;
	return ofr;
}

static void addToOpenFileList(OpenFileRequest *ofr){
	acquireLock(&openFileLock);
	ADD_TO_DQUEUE(ofr, &openFileList);
	releaseLock(&openFileLock);
}
static void removeFromOpenFileList(OpenFileRequest *ofr){
	acquireLock(&openFileLock);
	REMOVE_FROM_DQUEUE(ofr);
	releaseLock(&openFileLock);
}

static void finishFileIO(OpenFileRequest *ofr, uintptr_t returnValue){
	assert(ofr->isReady = 1);
	ofr->isReady = 0;
	ofr->lastReturnValue = returnValue;
	finishIO(&ofr->ior);
}

static void closeFileRequest(IORequest *ior){
	OpenFileRequest *of = ior->ioRequest;
	removeFromOpenFileList(of);
	DELETE(of);
}

static int finishOpenFileRequest(IORequest *ior, uintptr_t *returnValues){
	OpenFileRequest *ofr = ior->ioRequest;
	returnValues[0] = ofr->handle;
	if(ofr->isClosing){
		closeFileRequest(ior);
		return 1;
	}
	else{
		pendIO(ior);
		ofr->isReady = 1;
		returnValues[1] = ofr->lastReturnValue;
		setCancellable(ior, 1);
		return 2;
	}
}

static OpenFileRequest *createOpenFileRequest(const BLOBAddress *file, Task *task){
	OpenFileRequest *NEW(ofr);
	if(ofr == NULL)
		return NULL;
	initIORequest(&ofr->ior, ofr, closeFileRequest, finishOpenFileRequest);
	ofr->file = file;
	ofr->isReady = 0;
	ofr->isClosing = 0;
	ofr->lastReturnValue = 0;
	ofr->offset = 0;
	ofr->handle = (uintptr_t)&ofr->handle;
	ofr->task = task;
	ofr->next = NULL;
	ofr->prev = NULL;
	return ofr;
}

static IORequest *openKFS(const char *fileName, uintptr_t length){
	//TODO: check fileName address
	const BLOBAddress *file;
	for(file = blobList; file != blobList + blobCount; file++){
		if(strncmp(fileName, file->name, length) == 0){
			break;
		}
	}
	// file not found
	if(file == blobList + blobCount)
		return IO_REQUEST_FAILURE;
	OpenFileRequest *ofr = createOpenFileRequest(file, processorLocalTask());
	setCancellable(&ofr->ior, 0);
	pendIO(&ofr->ior);
	addToOpenFileList(ofr);

	finishFileIO(ofr, 0);
	return &ofr->ior;
}

static IORequest *readKFS(void *arg, uint8_t *buffer, uintptr_t bufferSize){
	//TODO: check buffer address
	OpenFileRequest *ofr = arg;
	setCancellable(&ofr->ior, 0);
	uintptr_t copySize = MIN(bufferSize, ofr->file->end - ofr->file->begin - ofr->offset);
	memcpy(buffer, (void*)(ofr->file->begin + ofr->offset), copySize);
	ofr->offset += copySize;
	finishFileIO(ofr, copySize);
	return &ofr->ior;
}

static IORequest *closeKFS(void *arg){
	OpenFileRequest *ofr = arg;
	ofr->isClosing = 1;
	ofr->isReady = 0;
	finishFileIO(ofr, 0);
	return &ofr->ior;
}

static void kernelFileServiceHandler(InterruptParam *p){
	sti();
	FileFunctions ff;
	ff.open = openKFS;
	ff.read = readKFS;
	ff.write = NULL;
	ff.seek = NULL;
	ff.close = closeKFS;
	ff.checkHandle = searchOpenFileList;
	uintptr_t fileRequest = dispatchFileSystemCall(p, &ff);
	SYSTEM_CALL_RETURN_VALUE_0(p) = fileRequest;
}

#define KERNEL_FILE_SERVICE_NAME ("kernelfs")

void kernelFileService(void){
	int kfs = registerService(global.syscallTable,
		KERNEL_FILE_SERVICE_NAME, kernelFileServiceHandler, 0);
	if(kfs <= 0){
		systemCall_terminate();
	}
	int ok = addFileSystem(kfs, KERNEL_FILE_SERVICE_NAME, strlen(KERNEL_FILE_SERVICE_NAME));
	if(!ok){
		systemCall_terminate();
	}
	while(1){
		sleep(1000);
	}
}

static void testListKFS(void){
	int a;
	printk("list of files: (total %d)\n", blobCount);
	for(a = 0; a < blobCount; a++){
		printk("%s %x %x\n", blobList[a].name, blobList[a].begin, blobList[a].end);
		uintptr_t b;
		for(b = blobList[a].begin; b < blobList[a].end; b++){
			printk("%c", (*(const char*)b));
		}
		printk("\n");
	}
}

void testKFS(void);
void testKFS(void){
	uintptr_t r = systemCall_discoverFileSystem(KERNEL_FILE_SERVICE_NAME, strlen(KERNEL_FILE_SERVICE_NAME));
	assert(r != IO_REQUEST_FAILURE);
	int kfs;
	uintptr_t r2 = systemCall_waitIOReturn(r, 1, &kfs);
	assert(r == r2);
	testListKFS();
	// file not exist
	r = systemCall_openFile(kfs, "abcdefg", strlen("abcdefg"));
	assert(r == IO_REQUEST_FAILURE);
	// open file
	r = systemCall_openFile(kfs, "testfile.txt", strlen("testfile.txt"));
	assert(r != IO_REQUEST_FAILURE);
	uintptr_t file, file2;
	r2 = systemCall_waitIOReturn(r, 1, &file);
	assert(r == r2);
	//read
	int i;
	for(i = 0; i < 3; i++){
		char x[12];
		file2 = 99;
		MEMSET0(x);
		// read file
		r2 = systemCall_readFile(kfs, file, x, 11);
		assert(r == r2);
		// last operation is not finished
		r2 = systemCall_readFile(kfs, file, x, 11);
		assert(r2 == IO_REQUEST_FAILURE);
		uintptr_t readCount = 10000;
		r2 = systemCall_waitIOReturn(r, 2, &file2, &readCount);
		assert(r2 == r && file2 == file);
		x[readCount] = '\0';
		printk("%s %d\n",x, readCount);
	}
	// close
	r2 = systemCall_closeFile(kfs, file);
	assert(r2 == r);
	// last operation is not finished
	r2 = systemCall_closeFile(kfs, file);
	assert(r2 == IO_REQUEST_FAILURE);
	r2 = systemCall_waitIOReturn(r, 1, &file2);
	assert(r2 == r && file == file2);
	// not opened
	r2 = systemCall_readFile(kfs, file, &r2, 1);
	assert(r2 == IO_REQUEST_FAILURE);
	printk("testKFS ok\n");
	systemCall_terminate();
}
