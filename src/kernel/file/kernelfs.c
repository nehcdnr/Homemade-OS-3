#include"file.h"
#include"interrupt/handler.h"
#include"interrupt/systemcall.h"
#include"memory/memory.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"
#include"io/io.h"
#include<blob.h>

typedef struct{
	OpenedFile of;
	const BLOBAddress *blob;
	uintptr_t offset;
}OpenedBLOBFile;

static void acceptCloseKF(void *instance){
	OpenedBLOBFile *f = instance;
	DELETE(f);
}

static void acceptKFRequest(void *instance){
	DELETE(instance);
}

static RWFileRequest *readKFS(OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize){
	void *mappedPage;
	void *mappedBuffer;
	EXPECT(mapBufferToKernel(buffer, bufferSize, &mappedPage, &mappedBuffer));

	OpenedBLOBFile *f = of->instance;
	RWFileRequest *NEW(fior1);
	EXPECT(fior1 != NULL);
	initRWFileIO(fior1, fior1, of, notSupportCancelFileIO, acceptKFRequest);

	pendIO(&fior1->fior.ior);
	uintptr_t copySize = MIN(bufferSize, f->blob->end - f->blob->begin - f->offset);
	memcpy(buffer, (void*)(f->blob->begin + f->offset), copySize);

	unmapPages(kernelLinear, mappedPage);
	f->offset += copySize;
	completeRWFileIO(fior1, copySize);
	return fior1;

	ON_ERROR;
	unmapPages(kernelLinear, mappedPage);
	ON_ERROR;
	return NULL;
}

static RWFileRequest *enumReadKFS(OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize){
	if(bufferSize < sizeof(FileEnumeration))
		return IO_REQUEST_FAILURE;
	void *mappedPage;
	void *mappedBuffer;
	EXPECT(mapBufferToKernel(buffer, bufferSize, &mappedPage, &mappedBuffer));

	OpenedBLOBFile *f = of->instance;
	RWFileRequest *NEW(fior1);
	EXPECT(fior1 != NULL);
	initRWFileIO(fior1, fior1, of, notSupportCancelFileIO, acceptKFRequest);

	pendIO(&fior1->fior.ior);
	BLOBAddress *entry = (BLOBAddress*)(f->blob->begin + f->offset);
	assert((f->blob->end - (uintptr_t)entry) % sizeof(*entry) == 0);
	uintptr_t readSize;
	if(f->blob->end > (uintptr_t)entry){
		readSize = sizeof(FileEnumeration);
		initFileEnumeration(mappedBuffer, entry->name);
	}
	else{
		readSize = 0;
	}
	unmapPages(kernelLinear, mappedPage);
	f->offset += sizeof(*entry);
	completeRWFileIO(fior1, readSize);
	return fior1;

	ON_ERROR;
	unmapPages(kernelLinear, mappedPage);
	ON_ERROR;
	return NULL;

}

static FileIORequest0 *seekKFS(OpenedFile *of, uint64_t position){
	OpenedBLOBFile *f = of->instance;
	FileIORequest0 *NEW(fior0);
	EXPECT(fior0 != NULL);
	initFileIO0(fior0, fior0, of, notSupportCancelFileIO, acceptKFRequest);
	if(position > f->blob->end - f->blob->begin){
		return IO_REQUEST_FAILURE;
	}
	pendIO(&fior0->fior.ior);
	f->offset = (uintptr_t)position;
	completeFileIO0(fior0);
	return fior0;

	ON_ERROR;
	return NULL;
}

static FileIORequest2 *sizeOfKFS(OpenedFile *of){
	OpenedBLOBFile *f = of->instance;
	FileIORequest2 *NEW(fior2);
	EXPECT(fior2 != NULL);
	initFileIO2(fior2, fior2, of, notSupportCancelFileIO, acceptKFRequest);
	pendIO(&fior2->fior.ior);
	completeFileIO64(fior2, f->blob->end - f->blob->begin);
	return fior2;

	ON_ERROR;
	return NULL;
}

static CloseFileRequest *closeKFS(OpenedFile *of){
	CloseFileRequest *cfr = setCloseFileIO(of, of->instance, acceptCloseKF);
	pendIO(&cfr->cfior.ior);
	completeCloseFile(cfr);
	return cfr;
}

static OpenedBLOBFile *createOpenedBLOBFile(const BLOBAddress *blob, const FileFunctions *func){
	OpenedBLOBFile *NEW(f);
	if(f == NULL)
		return NULL;
	initOpenedFile(&f->of, f, func);
	f->blob = blob;
	f->offset = 0;
	return f;
}

static const BLOBAddress *findByName(const char *fileName, uintptr_t length){
	const BLOBAddress *file;
	for(file = blobList; file != blobList + blobCount; file++){
		if(strncmp(fileName, file->name, length) == 0){
			break;
		}
	}
	if(file == blobList + blobCount)
		return NULL;
	else return file;
}

static BLOBAddress kfDirectory;

static OpenFileRequest *openKFS(const char *fileName, uintptr_t length, OpenFileMode mode){
	const BLOBAddress *blobAddress;
	if(mode.enumeration == 0){
		blobAddress = findByName(fileName, length);
	}
	else{
		blobAddress = (length == 0? &kfDirectory: NULL);
	}
	EXPECT(blobAddress != NULL);

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

	// see acceptClose
	OpenedBLOBFile *f = createOpenedBLOBFile(blobAddress, &func);
	EXPECT(f != NULL);
	OpenFileRequest *NEW(fior);
	EXPECT(fior != NULL);
	initOpenFileIO(fior, fior, notSupportCancelFileIO, acceptKFRequest);

	pendIO(&fior->ofior.ior);
	completeOpenFile(fior, &f->of);
	return fior;
	//DELETE(fior1)
	ON_ERROR;
	DELETE(f);
	ON_ERROR;
	ON_ERROR;
	return NULL;
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
		r = systemCall_readFile(file, x, 11);
		assert(r != IO_REQUEST_FAILURE);
		// last operation is not accepted
		// r2 = systemCall_readFile(file, x, 11);
		// assert(r2 == IO_REQUEST_FAILURE);
		uintptr_t readCount = 10000;
		r2 = systemCall_waitIOReturn(r, 1, &readCount);
		assert(r == r2);
		x[readCount] = '\0';
		printk("%s %d\n",x, readCount);
		r = systemCall_seekFile(file, (i + 1) * 5);
		assert(r != IO_REQUEST_FAILURE);
		r2 = systemCall_waitIO(r);
		assert(r == r2);
	}
	// close
	r = systemCall_closeFile(file);
	assert(r != IO_REQUEST_FAILURE);
	// close again
	r2 = systemCall_closeFile(file);
	assert(r2 == IO_REQUEST_FAILURE);
	r2 = systemCall_waitIO(r);
	assert(r == r2);
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
