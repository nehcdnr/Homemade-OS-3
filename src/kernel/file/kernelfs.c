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

static int finishOpenFileRequest(IORequest *ior, uintptr_t *returnValues){
	KFRequest *kfr = ior->instance;

	int returnCount = kfr->lastReturnCount;
	memcpy(returnValues, kfr->lastReturnValues, kfr->lastReturnCount * sizeof(returnValues[0]));
	if(kfr->isClosing){
		removeFromOpenFileList(getOpenFileManager(processorLocalTask()), &kfr->ofr);
		DELETE(kfr);
	}
	else{
		kfr->isReady = 1;
	}
	return returnCount;
}

static IORequest *readKFS(OpenFileRequest *ofr, uint8_t *buffer, uintptr_t bufferSize){
	void *mappedPage;
	void *mappedBuffer;
	if(mapBufferToKernel(buffer, bufferSize, &mappedPage, &mappedBuffer) == 0)
		return IO_REQUEST_FAILURE;

	KFRequest *kfr = ofr->instance;
	pendIO(&kfr->ior);
	uintptr_t copySize = MIN(bufferSize, kfr->file->end - kfr->file->begin - kfr->offset);
	memcpy(buffer, (void*)(kfr->file->begin + kfr->offset), copySize);

	unmapPages(kernelLinear, mappedPage);
	kfr->offset += copySize;
	FINISH_FILE_IO_2(kfr, copySize);
	return &kfr->ior;
}

static IORequest *enumReadKFS(OpenFileRequest *ofr, uint8_t *buffer, uintptr_t bufferSize){
	if(bufferSize < sizeof(FileEnumeration))
		return IO_REQUEST_FAILURE;
	void *mappedPage;
	void *mappedBuffer;
	if(mapBufferToKernel(buffer, bufferSize, &mappedPage, &mappedBuffer) == 0)
		return IO_REQUEST_FAILURE;

	KFRequest *kfr = ofr->instance;
	pendIO(&kfr->ior);
	BLOBAddress *entry = (BLOBAddress*)(kfr->file->begin + kfr->offset);
	assert((kfr->file->end - (uintptr_t)entry) % sizeof(*entry) == 0);
	uintptr_t readSize;
	if(kfr->file->end > (uintptr_t)entry){
		readSize = sizeof(FileEnumeration);
		initFileEnumeration(mappedBuffer, entry->name);
	}
	else{
		readSize = 0;
	}
	unmapPages(kernelLinear, mappedPage);
	kfr->offset += sizeof(*entry);
	FINISH_FILE_IO_2(kfr, readSize);
	return &kfr->ior;
}

static IORequest *seekKFS(OpenFileRequest *ofr, uint64_t position){
	KFRequest *kfr = ofr->instance;
	if(position > kfr->file->end - kfr->file->begin){
		return IO_REQUEST_FAILURE;
	}
	pendIO(&kfr->ior);
	kfr->offset = (uintptr_t)position;
	FINISH_FILE_IO_1(kfr);
	return &kfr->ior;
}

static IORequest *sizeOfKFS(OpenFileRequest *ofr){
	KFRequest *kfr = ofr->instance;
	pendIO(&kfr->ior);
	uint64_t s = (kfr->file->end - kfr->file->begin);
	FINISH_FILE_IO_3(kfr, (uint32_t)(s & 0xffffffff), (uint32_t)((s >> 32) & 0xffffffff));
	return &kfr->ior;
}

static IORequest *closeKFS(OpenFileRequest *ofr){
	KFRequest *kfr = ofr->instance;
	pendIO(&kfr->ior);
	kfr->isClosing = 1;
	FINISH_FILE_IO_1(kfr);
	return &kfr->ior;
}

static KFRequest *createKFRequest(const BLOBAddress *file, const FileFunctions *func){
	KFRequest *NEW(kfr);
	if(kfr == NULL)
		return NULL;
	initIORequest(&kfr->ior, kfr, notSupportCancelIORequest, finishOpenFileRequest);
	initOpenFileRequest(&kfr->ofr, kfr, func);
	kfr->file = file;
	kfr->isReady = 1;
	kfr->isClosing = 0;
	memset(kfr->lastReturnValues, 0, sizeof(kfr->lastReturnValues));
	kfr->lastReturnCount = 0;
	kfr->offset = 0;
	return kfr;
}

static const BLOBAddress *mapAndFindByName(const char *fileName, uintptr_t length){
	void *mappedPage;
	void *mappedFileName;
	if(mapBufferToKernel(fileName, length, &mappedPage, &mappedFileName) == 0)
		return NULL;

	const BLOBAddress *file;
	for(file = blobList; file != blobList + blobCount; file++){
		if(strncmp(mappedFileName, file->name, length) == 0){
			break;
		}
	}
	unmapPages(kernelLinear, mappedPage);
	if(file == blobList + blobCount)
		return NULL;
	else return file;
}

static BLOBAddress kfDirectory;

static IORequest *openKFS(const char *fileName, uintptr_t length, OpenFileMode mode){
	const BLOBAddress *file;
	if(mode.enumeration == 0){
		file = mapAndFindByName(fileName, length);
	}
	else{
		file = (length == 0? &kfDirectory: NULL);
	}
	if(file == NULL)
		return IO_REQUEST_FAILURE;

	static FileFunctions func = INITIAL_FILE_FUNCTIONS;
	if(mode.enumeration == 0){
		func.read = readKFS;
		func.seek = seekKFS;
		func.sizeOf = sizeOfKFS;
	}
	else{
		func.read = enumReadKFS;
	}
	func.close = closeKFS;
	func.isValidFile = isValidKFRequest;

	KFRequest *kfr = createKFRequest(file, &func);
	if(kfr == NULL)
		return IO_REQUEST_FAILURE;
	pendIO(&kfr->ior);
	addToOpenFileList(getOpenFileManager(processorLocalTask()), &kfr->ofr);
	FINISH_FILE_IO_2(kfr, kfr->ofr.handle);
	return &kfr->ior;
}

void kernelFileService(void){
	kfDirectory.name = "";
	kfDirectory.begin = (uintptr_t)blobList;
	kfDirectory.end = (uintptr_t)(blobList + blobCount);

	FileNameFunctions ff = INITIAL_FILE_NAME_FUNCTIONS;
	ff.open = openKFS;
	int ok = addFileSystem(&ff, "kernelfs", strlen("kernelfs"));
	if(!ok){
		systemCall_terminate();
	}
	while(1){
		sleep(1000);
	}
}
#ifndef NDEBUG
static void testListKFS(void){
	int a;
	uintptr_t r, readSize;
	uintptr_t fileHandle = syncEnumerateFile("kernelfs:");
	assert(fileHandle != IO_REQUEST_FAILURE);
	FileEnumeration fe;
	readSize = sizeof(fe) - 1;
	r = syncReadFile(fileHandle, &fe, &readSize);
	assert(r == IO_REQUEST_FAILURE);
	for(a = 0; 1; a++){
		readSize = sizeof(fe);
		r = syncReadFile(fileHandle, &fe, &readSize);
		assert(r == fileHandle && readSize % sizeof(fe) == 0);
		if(readSize == 0)
			break;
		uintptr_t  i;
		for(i = 0; i < fe.nameLength; i++){
			printk("%c", fe.name[i]);
		}
		printk(" %d\n", fe.nameLength);
	}
	assert(a == blobCount);
	printk("end of file list: (total %d)\n", a);
}

void testKFS(void);
void testKFS(void){
	uintptr_t r, r2;
	testListKFS();
	// file not exist
	const char *f1 = "kernelfs:abcdefg.txt", *f2 = "kernelfs:testfile.txt";
	r = systemCall_openFile(f1, strlen(f1), OPEN_FILE_MODE_0);
	assert(r == IO_REQUEST_FAILURE);
	// open file
	r = systemCall_openFile(f2, strlen(f2), OPEN_FILE_MODE_0);
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

	// test auto close file
	file = syncOpenFile(f2);
	assert(file != IO_REQUEST_FAILURE);
	printk("testKFS ok\n");
	systemCall_terminate();
}
#endif
