#include"file.h"
#include"interrupt/handler.h"
#include"interrupt/systemcall.h"
#include"memory/memory.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"
#include"io/io.h"
#include<blob.h>

typedef struct KFRequest{
	IORequest ior;
	OpenFileRequest ofr;

	int isReady;
	int isClosing;
	int lastReturnCount;
	uintptr_t lastReturnValues[SYSTEM_CALL_MAX_RETURN_COUNT];

	const BLOBAddress *file;
	uintptr_t offset;
}KFRequest;

static int isValidKFRequest(OpenFileRequest *ofr){
	KFRequest *kfr = ofr->instance;
	assert(ofr->task == processorLocalTask());
	return kfr->isReady;
}

static void _finishFileIO(KFRequest *kfr, int returnCount, ...){
	assert(kfr->isReady == 1);
	kfr->isReady = 0;
	kfr->lastReturnCount = returnCount;
	va_list va;
	va_start(va, returnCount);
	int i;
	for(i = 0; i < returnCount; i++){
		kfr->lastReturnValues[i] = va_arg(va, uintptr_t);
	}
	va_end(va);

	finishIO(&kfr->ior);
}
#define FINISH_FILE_IO_1(OFR) _finishFileIO(OFR, 0)
#define FINISH_FILE_IO_2(OFR, V0) _finishFileIO(OFR, 1, (V0))
#define FINISH_FILE_IO_3(OFR, V0, V1) _finishFileIO(OFR, 2, (V0), (V1))

static void closeFileRequest(IORequest *ior){
	KFRequest *of = ior->instance;
	removeFromOpenFileList(&of->ofr);
	DELETE(of);
}

static int finishOpenFileRequest(IORequest *ior, uintptr_t *returnValues){
	KFRequest *kfr = ior->instance;

	int returnCount = kfr->lastReturnCount;
	memcpy(returnValues, kfr->lastReturnValues, kfr->lastReturnCount * sizeof(returnValues[0]));
	if(kfr->isClosing){
		closeFileRequest(ior);
	}
	else{
		pendIO(ior);
		kfr->isReady = 1;
		setCancellable(ior, 1);
	}
	return returnCount;
}

static KFRequest *createKFRequest(const BLOBAddress *file, Task *task);

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
	KFRequest *kfr = createKFRequest(file, processorLocalTask());
	if(kfr == NULL)
		return IO_REQUEST_FAILURE;
	setCancellable(&kfr->ior, 0);
	pendIO(&kfr->ior);
	addToOpenFileList(&kfr->ofr);
	kfr->isReady = 1;
	FINISH_FILE_IO_2(kfr, kfr->ofr.handle);
	return &kfr->ior;
}

static IORequest *readKFS(OpenFileRequest *ofr, uint8_t *buffer, uintptr_t bufferSize){
	void *mappedPage;
	void *mappedBuffer;
	if(mapBufferToKernel(buffer, bufferSize, &mappedPage, &mappedBuffer) == 0)
		return IO_REQUEST_FAILURE;

	KFRequest *kfr = ofr->instance;
	setCancellable(&kfr->ior, 0);
	uintptr_t copySize = MIN(bufferSize, kfr->file->end - kfr->file->begin - kfr->offset);
	memcpy(buffer, (void*)(kfr->file->begin + kfr->offset), copySize);

	unmapPages(kernelLinear, mappedPage);
	kfr->offset += copySize;
	FINISH_FILE_IO_2(kfr, copySize);
	return &kfr->ior;
}

static IORequest *seekKFS(OpenFileRequest *ofr, uint64_t position){
	KFRequest *kfr = ofr->instance;
	if(position > kfr->file->end - kfr->file->begin){
		return IO_REQUEST_FAILURE;
	}
	setCancellable(&kfr->ior, 0);
	kfr->offset = (uintptr_t)position;
	FINISH_FILE_IO_1(kfr);
	return &kfr->ior;
}

static IORequest *sizeOfKFS(OpenFileRequest *ofr){
	KFRequest *kfr = ofr->instance;
	uint64_t s = (kfr->file->end - kfr->file->begin);
	FINISH_FILE_IO_3(kfr, (uint32_t)(s & 0xffffffff), (uint32_t)((s >> 32) & 0xffffffff));
	return &kfr->ior;
}

static IORequest *closeKFS(OpenFileRequest *ofr){
	KFRequest *kfr = ofr->instance;
	kfr->isClosing = 1;
	FINISH_FILE_IO_1(kfr);
	return &kfr->ior;
}

static const FileFunctions kernelFileFunctions = INITIAL_FILE_FUNCTIONS(
	openKFS,
	readKFS,
	NULL,
	seekKFS,
	NULL,
	NULL,
	sizeOfKFS,
	closeKFS,
	isValidKFRequest
);

static KFRequest *createKFRequest(const BLOBAddress *file, Task *task){
	KFRequest *NEW(kfr);
	if(kfr == NULL)
		return NULL;
	initIORequest(&kfr->ior, kfr, closeFileRequest, finishOpenFileRequest);
	initOpenFileRequest(&kfr->ofr, kfr, task, &kernelFileFunctions);
	kfr->file = file;
	kfr->isReady = 0;
	kfr->isClosing = 0;
	MEMSET0(kfr->lastReturnValues);
	kfr->lastReturnCount = 0;
	kfr->offset = 0;
	return kfr;
}

void kernelFileService(void){
	int ok = addFileSystem(openKFS, KERNEL_FILE_SERVICE_NAME, strlen(KERNEL_FILE_SERVICE_NAME));
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
		for(b = blobList[a].begin; b < blobList[a].end && b < blobList[a].begin + 10; b++){
			printk("%c", (*(const char*)b));
		}
		printk("\n");
	}
}

void testKFS(void);
void testKFS(void){
	uintptr_t r, r2;
	testListKFS();
	// file not exist
	const char *f1 = "kernelfs:abcdefg.txt", *f2 = "kernelfs:testfile.txt";
	r = systemCall_openFile(f1, strlen(f1));
	assert(r == IO_REQUEST_FAILURE);
	// open file
	r = systemCall_openFile(f2, strlen(f2));
	assert(r != IO_REQUEST_FAILURE);
	uintptr_t file;
	r2 = systemCall_waitIOReturn(r, 1, &file);
	assert(r == r2);
	//sizeOf
	r = systemCall_sizeOfFile(file);
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
		r2 = systemCall_readFile(file, x, 11);
		assert(r == r2);
		// last operation is not finished
		r2 = systemCall_readFile(file, x, 11);
		assert(r2 == IO_REQUEST_FAILURE);
		uintptr_t readCount = 10000;
		r2 = systemCall_waitIOReturn(r, 1, &readCount);
		assert(r2 == r);
		x[readCount] = '\0';
		printk("%s %d\n",x, readCount);
		r2 = systemCall_seekFile(file, (i + 1) * 5);
		assert(r == r2);
		r2 = systemCall_waitIO(r);
		assert(r2 == r);
	}
	// close
	r2 = systemCall_closeFile(file);
	assert(r2 == r);
	// last operation is not finished
	r2 = systemCall_closeFile(file);
	assert(r2 == IO_REQUEST_FAILURE);
	r2 = systemCall_waitIO(r);
	assert(r2 == r);
	// not opened
	r2 = systemCall_readFile(file, &r2, 1);
	assert(r2 == IO_REQUEST_FAILURE);
	printk("testKFS ok\n");
	systemCall_terminate();
}
