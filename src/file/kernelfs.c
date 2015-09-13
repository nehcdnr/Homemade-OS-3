#include"file.h"
#include"interrupt/handler.h"
#include"interrupt/systemcall.h"
#include"memory/memory.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"
#include"io/io.h"

typedef struct OpenFileRequest OpenFileRequest;
struct OpenFileRequest{
	IORequest ior;
	int isReady;
	int isClosing;
	uintptr_t handle;
	OpenFileRequest *next, **prev;
};


static OpenFileRequest *openFileList = NULL;
static Spinlock openFileLock = INITIAL_SPINLOCK;

static void *searchReadyOpenFileList(uintptr_t handle){
	OpenFileRequest *ofr;
	acquireLock(&openFileLock);
	for(ofr = openFileList; ofr != NULL; ofr = ofr->next){
		if(ofr->handle == handle)
			break;
	}
	releaseLock(&openFileLock);
	if(ofr == NULL)
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
	}
	else{
		pendIO(ior);
		ofr->isReady = 1;
		setCancellable(ior, 1);
	}
	return 1;
}

static OpenFileRequest *createOpenFileRequest(void){
	OpenFileRequest *NEW(ofr);
	if(ofr == NULL)
		return NULL;
	initIORequest(&ofr->ior, ofr, closeFileRequest, finishOpenFileRequest);
	ofr->isReady = 0;
	ofr->isClosing = 0;
	ofr->handle = (uintptr_t)&ofr->handle;
	ofr->next = NULL;
	ofr->prev = NULL;
	return ofr;
}

static IORequest *testOpenKFS(const char *fileName, uintptr_t length){
	//TODO: check fileName address
	if((uintptr_t)strlen("null") != length || strncmp(fileName, "null", length) != 0){
		return IO_REQUEST_FAILURE;
	}
	OpenFileRequest *ofr = createOpenFileRequest();
	setCancellable(&ofr->ior, 0);
	pendIO(&ofr->ior);
	addToOpenFileList(ofr);

	finishIO(&ofr->ior);
	return &ofr->ior;
}

static IORequest *testReadKFS(void *arg, uint8_t *buffer, uintptr_t bufferSize){
	OpenFileRequest *ofr = arg;
	assert(ofr->isReady = 1);
	ofr->isReady = 0;
	setCancellable(&ofr->ior, 0);
	uintptr_t i;
	for(i = 0; i < bufferSize; i++){
		buffer[i] = i % 26 + 'a';
	}

	finishIO(&ofr->ior);
	return &ofr->ior;
}

static IORequest *testCloseKFS(void *arg){
	OpenFileRequest *ofr = arg;
	ofr->isClosing = 1;

	finishIO(&ofr->ior);
	return &ofr->ior;
}

static void kernelFileServiceHandler(InterruptParam *p){
	sti();
	FileFunctions ff;
	ff.open = testOpenKFS;
	ff.read = testReadKFS;
	ff.write = NULL;
	ff.seek = NULL;
	ff.close = testCloseKFS;
	ff.checkHandle = searchReadyOpenFileList;
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

void testKFS(void);
void testKFS(void){
	uintptr_t r = systemCall_discoverFileSystem(KERNEL_FILE_SERVICE_NAME, strlen(KERNEL_FILE_SERVICE_NAME));
	assert(r != IO_REQUEST_FAILURE);
	int kfs;
	uintptr_t r2 = systemCall_waitIOReturn(r, 1, &kfs);
	assert(r == r2);
	// file not exist
	r = systemCall_openFile(kfs, "abcdefg", strlen("abcdefg"));
	assert(r == IO_REQUEST_FAILURE);
	// open file
	r = systemCall_openFile(kfs, "null", strlen("null"));
	assert(r != IO_REQUEST_FAILURE);
	uintptr_t file, file2;
	r2 = systemCall_waitIOReturn(r, 1, &file);
	assert(r == r2);
	//read
	int i;
	for(i = 0; i < 3; i++){
		char x[30];
		file2 = 99;
		MEMSET0(x);
		x[29] = '\0';
		// read file
		r2 = systemCall_readFile(kfs, file, x, 29);
		assert(r == r2);
		// last operation is not finished
		r2 = systemCall_readFile(kfs, file, x, 29);
		assert(r2 == IO_REQUEST_FAILURE);
		r2 = systemCall_waitIOReturn(r, 1, &file2);
		assert(r2 == r && file2 == file);
		printk("%s\n",x);
	}
	// close
	r2 = systemCall_closeFile(kfs, file);
	assert(r2 == r);
	r2 = systemCall_waitIOReturn(r, 1, &file2);
	assert(r2 == r && file == file2);
	// not opened
	r2 = systemCall_readFile(kfs, file, &r2, 1);
	assert(r2 == IO_REQUEST_FAILURE);
	printk("testKFS ok\n");
	systemCall_terminate();
}
