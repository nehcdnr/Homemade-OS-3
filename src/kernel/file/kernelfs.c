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
	int lastReturnCount;
	uintptr_t lastReturnValues[SYSTEM_CALL_MAX_RETURN_COUNT];

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

static void _finishFileIO(OpenFileRequest *ofr, int returnCount, ...){
	assert(ofr->isReady == 1);
	ofr->isReady = 0;
	ofr->lastReturnCount = returnCount;
	va_list va;
	va_start(va, returnCount);
	int i;
	for(i = 0; i < returnCount; i++){
		ofr->lastReturnValues[i] = va_arg(va, uintptr_t);
	}
	va_end(va);

	finishIO(&ofr->ior);
}
#define FINISH_FILE_IO_1(OFR) _finishFileIO(OFR, 0)
#define FINISH_FILE_IO_2(OFR, V0) _finishFileIO(OFR, 1, (V0))
#define FINISH_FILE_IO_3(OFR, V0, V1) _finishFileIO(OFR, 2, (V0), (V1))

static void closeFileRequest(IORequest *ior){
	OpenFileRequest *of = ior->instance;
	removeFromOpenFileList(of);
	DELETE(of);
}

static int finishOpenFileRequest(IORequest *ior, uintptr_t *returnValues){
	OpenFileRequest *ofr = ior->instance;

	int returnCount = ofr->lastReturnCount;
	memcpy(returnValues, ofr->lastReturnValues, ofr->lastReturnCount * sizeof(returnValues[0]));
	if(ofr->isClosing){
		closeFileRequest(ior);
	}
	else{
		pendIO(ior);
		ofr->isReady = 1;
		setCancellable(ior, 1);
	}
	return returnCount;
}

static OpenFileRequest *createOpenFileRequest(const BLOBAddress *file, Task *task){
	OpenFileRequest *NEW(ofr);
	if(ofr == NULL)
		return NULL;
	initIORequest(&ofr->ior, ofr, closeFileRequest, finishOpenFileRequest);
	ofr->file = file;
	ofr->isReady = 0;
	ofr->isClosing = 0;
	MEMSET0(ofr->lastReturnValues);
	ofr->lastReturnCount = 0;
	ofr->offset = 0;
	ofr->handle = (uintptr_t)&ofr->handle;
	ofr->task = task;
	ofr->next = NULL;
	ofr->prev = NULL;
	return ofr;
}

static void bufferToPageRange(uintptr_t bufferBegin, size_t bufferSize,
	uintptr_t *pageBegin, uintptr_t *pageOffset, size_t *pageSize){
	*pageBegin = FLOOR(bufferBegin, PAGE_SIZE);
	*pageOffset = bufferBegin - (*pageBegin);
	*pageSize = CEIL(bufferBegin + bufferSize, PAGE_SIZE) - (*pageBegin);
}

static int mapBufferToKernel(const void *buffer, uintptr_t size, void **mappedPage, void **mappedBuffer){
	uintptr_t pageOffset, pageBegin;
	size_t pageSize;
	bufferToPageRange((uintptr_t)buffer, size, &pageBegin, &pageOffset, &pageSize);
	*mappedPage = checkAndMapExistingPages(
		kernelLinear, isKernelLinearAddress(pageBegin)? kernelLinear: getTaskLinearMemory(processorLocalTask()),
		pageBegin, pageSize, KERNEL_PAGE, 0);
	if(*mappedPage == NULL){
		return 0;
	}
	*mappedBuffer = (void*)(pageOffset + ((uintptr_t)*mappedPage));
	return 1;
}

static IORequest *openKFS(const char *fileName, uintptr_t length){
	void *mappedPage;
	void *mappedFileName;
	if(mapBufferToKernel(fileName, length, &mappedPage, &mappedFileName) == 0)
		return IO_REQUEST_FAILURE;

	const BLOBAddress *file;
	for(file = blobList; file != blobList + blobCount; file++){
		if(strncmp(mappedFileName, file->name, length) == 0){
			break;
		}
	}
	unmapPages(kernelLinear, mappedPage);
	// file not found
	if(file == blobList + blobCount)
		return IO_REQUEST_FAILURE;
	OpenFileRequest *ofr = createOpenFileRequest(file, processorLocalTask());
	if(ofr == NULL)
		return IO_REQUEST_FAILURE;
	setCancellable(&ofr->ior, 0);
	pendIO(&ofr->ior);
	addToOpenFileList(ofr);
	ofr->isReady = 1;
	FINISH_FILE_IO_2(ofr, &ofr->handle);
	return &ofr->ior;
}

static IORequest *readKFS(void *arg, uint8_t *buffer, uintptr_t bufferSize){
	void *mappedPage;
	void *mappedBuffer;
	if(mapBufferToKernel(buffer, bufferSize, &mappedPage, &mappedBuffer) == 0)
		return IO_REQUEST_FAILURE;

	OpenFileRequest *ofr = arg;
	setCancellable(&ofr->ior, 0);
	uintptr_t copySize = MIN(bufferSize, ofr->file->end - ofr->file->begin - ofr->offset);
	memcpy(buffer, (void*)(ofr->file->begin + ofr->offset), copySize);

	unmapPages(kernelLinear, mappedPage);
	ofr->offset += copySize;
	FINISH_FILE_IO_2(ofr, copySize);
	return &ofr->ior;
}

static IORequest *seekKFS(void *arg, uint64_t position){
	OpenFileRequest *ofr = arg;
	if(position > ofr->file->end - ofr->file->begin){
		return IO_REQUEST_FAILURE;
	}
	setCancellable(&ofr->ior, 0);
	ofr->offset = (uintptr_t)position;
	FINISH_FILE_IO_1(ofr);
	return &ofr->ior;
}

static IORequest *sizeOfKFS(void *arg){
	OpenFileRequest *ofr = arg;
	uint64_t s = (ofr->file->end - ofr->file->begin);
	FINISH_FILE_IO_3(ofr, (uint32_t)(s & 0xffffffff), (uint32_t)((s >> 32) & 0xffffffff));
	return &ofr->ior;
}

static IORequest *closeKFS(void *arg){
	OpenFileRequest *ofr = arg;
	ofr->isClosing = 1;
	FINISH_FILE_IO_1(ofr);
	return &ofr->ior;
}

static void kernelFileServiceHandler(InterruptParam *p){
	sti();
	FileFunctions ff;
	ff.open = openKFS;
	ff.read = readKFS;
	ff.write = NULL;
	ff.seek = seekKFS;
	ff.sizeOf = sizeOfKFS;
	ff.close = closeKFS;
	ff.checkHandle = searchOpenFileList;
	uintptr_t fileRequest = dispatchFileSystemCall(p, &ff);
	SYSTEM_CALL_RETURN_VALUE_0(p) = fileRequest;
}

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
	uintptr_t file;
	r2 = systemCall_waitIOReturn(r, 1, &file);
	assert(r == r2);
	//sizeOf
	r = systemCall_sizeOfFile(kfs, file);
	uintptr_t sizeLow, sizeHigh;
	r2 = systemCall_waitIOReturn(r, 2, &sizeLow, &sizeHigh);
	printk("size = %d:%d\n",sizeHigh, sizeLow);
	assert(r == r2);
	//read
	int i;
	for(i = 0; i < 3; i++){
		char x[12];
		MEMSET0(x);
		// read file
		r2 = systemCall_readFile(kfs, file, x, 11);
		assert(r == r2);
		// last operation is not finished
		r2 = systemCall_readFile(kfs, file, x, 11);
		assert(r2 == IO_REQUEST_FAILURE);
		uintptr_t readCount = 10000;
		r2 = systemCall_waitIOReturn(r, 1, &readCount);
		assert(r2 == r);
		x[readCount] = '\0';
		printk("%s %d\n",x, readCount);
		r2 = systemCall_seekFile(kfs, file, (i + 1) * 5);
		assert(r == r2);
		r2 = systemCall_waitIO(r);
		assert(r2 == r);
	}
	// close
	r2 = systemCall_closeFile(kfs, file);
	assert(r2 == r);
	// last operation is not finished
	r2 = systemCall_closeFile(kfs, file);
	assert(r2 == IO_REQUEST_FAILURE);
	r2 = systemCall_waitIO(r);
	assert(r2 == r);
	// not opened
	r2 = systemCall_readFile(kfs, file, &r2, 1);
	assert(r2 == IO_REQUEST_FAILURE);
	printk("testKFS ok\n");
	systemCall_terminate();
}
