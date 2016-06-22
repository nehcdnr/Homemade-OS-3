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
	const BLOBAddress *blob;
}OpenedBLOBFile;

static uintptr_t computeCopySize(OpenedBLOBFile *f, uint64_t offset64, uintptr_t bufferSize){
	const uintptr_t fileSize = f->blob->end - f->blob->begin;
	if(offset64 > fileSize)
		return 0;
	return MIN(bufferSize, fileSize - ((uintptr_t)offset64));
}

static int seekReadKFS(
	RWFileRequest *fior1, OpenedFile *of,
	uint8_t *buffer, uint64_t offset64, uintptr_t bufferSize
){
	OpenedBLOBFile *f = getFileInstance(of);

	uintptr_t copySize = computeCopySize(f, offset64, bufferSize);
	memcpy(buffer, (void*)(f->blob->begin + ((uintptr_t)offset64)), copySize);

	completeRWFileIO(fior1, copySize, copySize);
	return 1;
}

static int enumReadKFS(RWFileRequest *fior1, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize){
	if(bufferSize < sizeof(FileEnumeration))
		return IO_REQUEST_FAILURE;

	OpenedBLOBFile *f = getFileInstance(of);;

	BLOBAddress *entry = (BLOBAddress*)(f->blob->begin + (uintptr_t)getFileOffset(of));
	assert((f->blob->end - (uintptr_t)entry) % sizeof(*entry) == 0);
	uintptr_t readSize;
	if(f->blob->end > (uintptr_t)entry){
		readSize = sizeof(FileEnumeration);
		initFileEnumeration((FileEnumeration*)buffer, entry->name, strlen(entry->name));
	}
	else{
		readSize = 0;
	}
	completeRWFileIO(fior1, readSize, sizeof(*entry));
	return 1;
}

static int getKFSParameter(FileIORequest2 *fior2, OpenedFile *of, uintptr_t parameterCode){
	OpenedBLOBFile *f = getFileInstance(of);
	switch(parameterCode){
	case FILE_PARAM_SIZE:
		completeFileIO64(fior2, f->blob->end - f->blob->begin);
		break;
	default:
		return 0;
	}
	return 1;
}

static void closeKFS(CloseFileRequest *cfr, OpenedFile *of){
	OpenedBLOBFile *f = getFileInstance(of);
	completeCloseFile(cfr);
	DELETE(f);
}

static OpenedBLOBFile *createOpenedBLOBFile(const BLOBAddress *blob){
	OpenedBLOBFile *NEW(f);
	if(f == NULL)
		return NULL;
	f->blob = blob;
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

static int openKFS(OpenFileRequest *fior, const char *fileName, uintptr_t length, OpenFileMode mode){
	const BLOBAddress *blobAddress;
	if(mode.enumeration == 0){
		blobAddress = findByName(fileName, length);
	}
	else{
		blobAddress = (length == 0? &kfDirectory: NULL);
	}
	EXPECT(blobAddress != NULL);
	// see closeKFS
	OpenedBLOBFile *f = createOpenedBLOBFile(blobAddress);
	EXPECT(f != NULL);

	FileFunctions func = INITIAL_FILE_FUNCTIONS;
	if(mode.enumeration == 0){
		func.read = seekReadByOffset;
		func.seekRead = seekReadKFS;
		func.getParameter = getKFSParameter;
	}
	else{
		func.read = enumReadKFS;
	}
	func.close = closeKFS;

	completeOpenFile(fior, f, &func);
	return 1;
	//DELETE(f);
	ON_ERROR;
	ON_ERROR;
	return 0;
}

void initKernelFile(void){
	kfDirectory.name = "";
	kfDirectory.begin = (uintptr_t)blobList;
	kfDirectory.end = (uintptr_t)(blobList + blobCount);

	FileNameFunctions ff = INITIAL_FILE_NAME_FUNCTIONS;
	ff.open = openKFS;
	int ok = addFileSystem(&ff, "kernelfs", strlen("kernelfs"));
	if(!ok){
		panic("cannot register kernel image as file system\n");
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
	//waitForFirstFileSystem
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
	r = systemCall_getFileParameter(file, FILE_PARAM_MAX_WRITE_SIZE);
	assert(r == IO_REQUEST_FAILURE);
	r = systemCall_getFileParameter(file, FILE_PARAM_SIZE);
	uintptr_t sizeLow, sizeHigh;
	r2 = systemCall_waitIOReturn(r, 2, &sizeLow, &sizeHigh);
	printk("size = %d:%d\n",sizeHigh, sizeLow);
	assert(r == r2);
	//read
	int i;
	for(i = 0; i < 3; i++){
		char x[8];
		MEMSET0(x);
		// read file
		r = systemCall_readFile(file, x, 7);
		assert(r != IO_REQUEST_FAILURE);
		// last operation is not accepted
		// r2 = systemCall_readFile(file, x, 11);
		// assert(r2 == IO_REQUEST_FAILURE);
		uintptr_t readCount = 10000;
		r2 = systemCall_waitIOReturn(r, 1, &readCount);
		assert(r == r2);
		x[readCount] = '\0';
		printk("%s %d\n", x, readCount);

		char x2[8];
		r = systemCall_seekReadFile(file, x2, i * 7, 7);
		assert(r != IO_REQUEST_FAILURE);
		uintptr_t readCount2 = 10000;
		r2 = systemCall_waitIOReturn(r, 1, &readCount2);
		assert(r == r2);
		x[readCount2] = '\0';
		if(readCount2 != readCount || strncmp(x, x2, readCount) != 0){
			assert(0);
		}
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
