#include"file/file.h"
#include"task/exclusivelock.h"
#include"kernel.h"
#include"io/fifo.h"

typedef struct FIFOElement{
	struct FIFOElement *next;
	uintptr_t bufferSize;
	uint8_t buffer[];
}FIFOElement;

static FIFOElement *createFIFOElement(const uint8_t *buffer, uintptr_t bufferSize){
	FIFOElement *e = allocateKernelMemory(sizeof(*e) + bufferSize);
	if(e == NULL){
		return NULL;
	}
	e->next = NULL;
	e->bufferSize = bufferSize;
	memcpy(e->buffer, buffer, bufferSize);
	return e;
}

static uintptr_t copyFIFOElement(FIFOElement *e, uint8_t *buffer, uintptr_t bufferSize){
	uintptr_t copySize = MIN(e->bufferSize, bufferSize);
	memcpy(buffer, e->buffer, copySize);
	return copySize;
}

static void deleteFIFOElement(FIFOElement *e){
	assert(e->next == NULL);
	releaseKernelMemory(e);
}

typedef struct FIFOList{
	FIFOElement *head, *tail;
}FIFOList;

const struct FIFOList initialFIFOList = {NULL, NULL};

static void pushFIFOElement_noLock(FIFOList *fifo, FIFOElement *e){
	e->next = NULL;
	if(fifo->head == NULL){
		fifo->head = e;
	}
	else{
		fifo->tail->next = e;
	}
	fifo->tail = e;
}

static FIFOElement *popFIFOElement_noLock(FIFOList *fifo){
	FIFOElement *e = fifo->head;
	assert(e != NULL);
	fifo->head = e->next;
	if(fifo->head == NULL){
		fifo->tail = NULL;
	}
	e->next = NULL;
	return e;
}

static int hasFIFOElement_noLock(FIFOList *fifo){
	return fifo->head != NULL;
}
/*
struct FIFOList{
	Semaphore *elementCount;
	Spinlock lock;
	struct FIFOHeadTail elementList;
};

int writeFIFOList(FIFOList *fifo, void *data, uintptr_t dataSize){
	FIFOElement *e = createFIFOElement(data, dataSize);
	if(e == NULL){
		return 0;
	}
	acquireLock(&fifo->lock);
	pushFIFOElement_noLock(&fifo->elementList, e);
	releaseLock(&fifo->lock);
	releaseSemaphore(fifo->elementCount);
	return 1;
}

uintptr_t readFIFOList(FIFOList *fifo, void *data, uintptr_t dataSize){
	acquireSemaphore(fifo->elementCount);
	acquireLock(&fifo->lock);
	FIFOElement *e = popFIFOElement_noLock(&fifo->elementList);
	releaseLock(&fifo->lock);
	uintptr_t copySize = copyFIFOElement(e, data, dataSize);
	deleteFIFOElement(e);
	return copySize;
}

FIFOList *createFIFOList(void){
	FIFOList *NEW(fifo);
	EXPECT(fifo != NULL);

	fifo->elementCount = createSemaphore(0);
	EXPECT(fifo->elementCount != NULL);
	fifo->lock = initialSpinlock;
	fifo->elementList = initialFIFOHeadTail;
	return fifo;
	//deleteSemaphore(fifo->elementCount);
	ON_ERROR;
	DELETE(fifo);
	ON_ERROR;
	return NULL;
}

void deleteFIFOList(FIFOList *fifo){
	assert(getSemaphoreValue(fifo->elementCount) == 0);
	deleteSemaphore(fifo->elementCount);
	DELETE(fifo);
}
*/
// file interface

typedef struct RWFIFORequest{
	RWFileRequest *rwfr;
	uint8_t *buffer;
	uintptr_t bufferSize;

	Spinlock *fifoLock;
	struct RWFIFORequest *next, **prev;
}RWFIFORequest;

static RWFIFORequest *createRWFIFORequest(RWFileRequest *rwfr, uint8_t *buffer, uintptr_t bufferSize){
	RWFIFORequest *NEW(r);
	r->rwfr = rwfr;
	r->buffer = buffer;
	r->bufferSize = bufferSize;
	r->fifoLock = NULL;
	r->next = NULL;
	r->prev = NULL;
	return r;
}

static void deleteRWFIFORequest(RWFIFORequest *r){
	assert(IS_IN_DQUEUE(r) == 0);
	DELETE(r);
}

typedef struct FIFOFile{
	Spinlock lock;
	struct FIFOList headTail;
	RWFIFORequest *pendingRead; // TODO: move to OpenedFile
}FIFOFile;

#define HAS_FIFO_REQUEST_NO_LOCK(F) ((F)->pendingRead != NULL)

static RWFIFORequest *popFIFORequest_noLock(FIFOFile *fifo){
	RWFIFORequest *r = fifo->pendingRead;
	assert(r != NULL);
	setRWFileIONotCancellable(r->rwfr);
	r->fifoLock = NULL;
	REMOVE_FROM_DQUEUE(r);
	return r;
}

static void cancelReadFIFO(void *arg){
	RWFIFORequest *r = arg;
	acquireLock(r->fifoLock);
	REMOVE_FROM_DQUEUE(r);
	releaseLock(r->fifoLock);
	deleteRWFIFORequest(r);
}

static void pushRWFIFORequest_noLock(FIFOFile *fifo, RWFIFORequest *r){
	r->fifoLock = &fifo->lock;
	ADD_TO_DQUEUE(r, &fifo->pendingRead);
	setRWFileIOCancellable(r->rwfr, r, cancelReadFIFO);
}

// return 1 if operation succeed
static void processFIFO(FIFOFile *fifo){
	while(1){
		FIFOElement *e = NULL;
		RWFIFORequest *r = NULL;
		acquireLock(&fifo->lock);
		if(hasFIFOElement_noLock(&fifo->headTail) && HAS_FIFO_REQUEST_NO_LOCK(fifo)){
			e = popFIFOElement_noLock(&fifo->headTail);
			r = popFIFORequest_noLock(fifo);
		}
		releaseLock(&fifo->lock);
		if(e == NULL || r == NULL){
			break;
		}
		uintptr_t copySize = copyFIFOElement(e, r->buffer, r->bufferSize);
		deleteFIFOElement(e);
		completeRWFileIO(r->rwfr, copySize, 0);
		deleteRWFIFORequest(r);
	}
}

static int readFIFOFile(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize){
	FIFOFile *fifo = getFileInstance(of);
	RWFIFORequest *r = createRWFIFORequest(rwfr, buffer, bufferSize);
	EXPECT(r != NULL);
	acquireLock(&fifo->lock);
	pushRWFIFORequest_noLock(fifo, r);
	releaseLock(&fifo->lock);
	processFIFO(fifo);
	return 1;
	ON_ERROR;
	return 0;
}

int directWriteFIFOFile(FIFOFile *fifo, const uint8_t *buffer, uintptr_t bufferSize){
	FIFOElement *e = createFIFOElement(buffer, bufferSize);
	EXPECT(e != NULL);
	acquireLock(&fifo->lock);
	pushFIFOElement_noLock(&fifo->headTail, e);
	releaseLock(&fifo->lock);
	processFIFO(fifo);
	return 1;
	ON_ERROR;
	return 0;
}

static int writeFIFOFile(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize){
	FIFOFile *fifo = getFileInstance(of);
	int ok = directWriteFIFOFile(fifo, buffer, bufferSize);
	if(ok){
		completeRWFileIO(rwfr, bufferSize, 0);
	}
	return ok;
}

static int getFIFOFileParam(FileIORequest2 *r2, OpenedFile *of, uintptr_t parameterCode){
	if(parameterCode == FILE_PARAM_FILE_INSTANCE){
		FIFOFile *fifo = getFileInstance(of);
		completeFileIO64(r2, (uint64_t)(uintptr_t)fifo);
		return 1;
	}
	return 0;
}

static void closeFIFOFile(CloseFileRequest *cfr, OpenedFile *of){
	FIFOFile *fifo = getFileInstance(of);
	while(1){
		acquireLock(&fifo->lock);
		FIFOElement *e = NULL;
		if(hasFIFOElement_noLock(&fifo->headTail)){
			e = popFIFOElement_noLock(&fifo->headTail);
		}
		releaseLock(&fifo->lock);
		if(e == NULL){
			break;
		}
		deleteFIFOElement(e);
	}
	assert(HAS_FIFO_REQUEST_NO_LOCK(fifo) == 0);
	completeCloseFile(cfr);
	DELETE(fifo);
}

static int openFIFOFile(
	OpenFileRequest *ofr,
	__attribute__((__unused__)) const char *fileName, uintptr_t nameLength,
	OpenFileMode ofm
){
	if(ofm.writable == 0 || nameLength != 0){
		return 0;
	}
	struct FIFOFile *NEW(fifo);
	fifo->lock = initialSpinlock;
	fifo->headTail = initialFIFOList;
	fifo->pendingRead = NULL;
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.read = readFIFOFile;
	ff.write = writeFIFOFile;
	ff.getParameter = getFIFOFileParam;
	ff.close = closeFIFOFile;
	completeOpenFile(ofr, fifo, &ff);
	return 1;
}

void initFIFOFile(void){
	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openFIFOFile;
	if(addFileSystem(&fnf, "fifo", strlen("fifo")) == 0){
		panic("cannot initialize FIFO file system");
	}
}

uintptr_t syncOpenFIFOFile(void){
	return syncOpenFileN("fifo:", strlen("fifo:"), OPEN_FILE_MODE_WRITABLE);
}

FIFOFile *syncGetFIFOFile(uintptr_t handle){
	uint64_t r = 0;
	if(syncGetFileParameter(handle, FILE_PARAM_FILE_INSTANCE, &r) == IO_REQUEST_FAILURE){
		return NULL;
	}
	else{
		return (FIFOFile*)(uintptr_t)r;
	}
}

#ifndef NDEBUG
#include"resource/resource.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"

struct TestArg{
	const uintptr_t fileHandle;
	volatile int writeCount, readCount;
};

static void testRWFIFO(struct TestArg *arg, int isWrite){
	const uint64_t delay = 50;
	uintptr_t f = arg->fileHandle;
	FIFOFile *fifo = syncGetFIFOFile(f);
	assert(fifo != NULL);
	uintptr_t r, s;
	// w->w->r->r
	if(isWrite){
		char buf[4] = "123";
		printk("test fifo w0\n");
		arg->writeCount++;
		s = 1;
		r = syncWriteFile(f, buf, &s);
		assert(r != IO_REQUEST_FAILURE && s == 1);
		printk("test fifo w1\n");
		arg->writeCount++;
		int ok = directWriteFIFOFile(fifo, (uint8_t*)(buf + 1), 2);
		assert(ok);
		assert(arg->readCount == 0);
		sleep(delay * 2);
		assert(arg->readCount == 2);
	}
	else{
		char buf[4] = "000";
		sleep(delay);
		assert(arg->writeCount == 2);
		printk("test fifo r0\n");
		s = 2;
		r = syncReadFile(f, buf, &s);
		assert(r != IO_REQUEST_FAILURE && s == 1 && buf[0] == '1');
		printk("test fifo r1\n");
		arg->readCount++;
		s = 1;
		r = syncReadFile(f, buf + 1, &s);
		assert(r != IO_REQUEST_FAILURE && s == 1 && buf[1] == '2' && buf[2] == '0');
		arg->readCount++;
	}
	// r->w
	if(isWrite){
		char buf[4] = "456";
		assert(arg->readCount == 2);
		sleep(delay);
		arg->writeCount++;
		s = 1;
		r = syncWriteFile(f, buf, &s);
		assert(r != IO_REQUEST_FAILURE && s == 1);
		printk("test fifo w2\n");
	}
	else{
		char buf[4] = "000";
		assert(arg->writeCount == 2);
		s = 1;
		r = syncReadFile(f, buf, &s);
		assert(r != IO_REQUEST_FAILURE && s == 1);
		printk("test fifo r2\n");
		arg->readCount++;
		assert(arg->writeCount == 3);
	}
	// cancel read
	if(isWrite == 0){
		char buf[4] = "000";
		r = systemCall_readFile(f, buf, 1);
		assert(r != IO_REQUEST_FAILURE);
		printk("test fifo r3\n");
		int ok = systemCall_cancelIO(r);
		assert(ok);
	}
	assert(r != IO_REQUEST_FAILURE && s == 1);
}

static void testWriteFIFO(void *arg){
	testRWFIFO(*(struct TestArg**)arg, 1);
	printk("test fifo file (w) ok\n");
	systemCall_terminate();
}

void testFIFOFile(void);
void testFIFOFile(void){
	int ok = waitForFirstResource("fifo", RESOURCE_FILE_SYSTEM, matchName);
	assert(ok);
	uintptr_t f = syncOpenFIFOFile();
	assert(f != IO_REQUEST_FAILURE);
	// systemCall_createUserThread
	struct TestArg arg = {f, 0, 0}, *argAddress = &arg;
	Task *t = createSharedMemoryTask(testWriteFIFO, &argAddress, sizeof(argAddress), processorLocalTask());
	assert(t != NULL);
	resume(t);
	testRWFIFO(argAddress, 0);
	f = syncCloseFile(f);
	assert(f != IO_REQUEST_FAILURE);
	printk("test fifo file (r) ok\n");
	systemCall_terminate();
}

#endif
