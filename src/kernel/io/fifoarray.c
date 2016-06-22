#include"fifo.h"
#include"common.h"
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"
#include"task/exclusivelock.h"
#include"file/file.h"

struct FIFO{
	uintptr_t begin, dataLength, bufferLength;
	Spinlock lock;
	uint8_t *buffer;

	Semaphore *semaphore;
	uintptr_t elmtSize;
};

static void dequeueBuffer(FIFO *fifo){
	fifo->begin = ((fifo->begin + 1) & (fifo->bufferLength - 1));
	fifo->dataLength -= 1;
}

static uint8_t *bufferLocation(FIFO *fifo, uintptr_t index){
	index = (index & (fifo->bufferLength - 1));
	return fifo->buffer + (fifo->elmtSize * index);
}

static int _writeFIFO(FIFO *fifo, void *data, void *overwrittenData){
	int sizeChangeFlag = 0, writeFlag = 1;
	acquireLock(&fifo->lock);
	sizeChangeFlag = (fifo->bufferLength > fifo->dataLength);
	if(sizeChangeFlag == 0){
		//printk("warning: fifo %x is full\n", fifo);
		if(overwrittenData != NULL){
			memcpy(overwrittenData, bufferLocation(fifo, fifo->begin), fifo->elmtSize);
			dequeueBuffer(fifo);
			writeFlag = 1;
		}
		else{
			writeFlag = 0;
		}
	}
	if(writeFlag){
		memcpy(bufferLocation(fifo, fifo->begin + fifo->dataLength), data, fifo->elmtSize);
		fifo->dataLength++;
	}
	releaseLock(&fifo->lock);
	return sizeChangeFlag;
}

int writeFIFO(FIFO *fifo, void *data){
	int r = _writeFIFO(fifo, data, NULL);
	if(r){
		releaseSemaphore(fifo->semaphore);
	}
	return r;
}

int overwriteFIFO(FIFO *fifo, void *data, void *overwrittenData){
	int r = _writeFIFO(fifo, data, overwrittenData);
	if(r){
		releaseSemaphore(fifo->semaphore);
	}
	return r;
}

static int _readFIFO(FIFO *fifo, void *data, int doReadFlag){
	uintptr_t r;
	acquireLock(&fifo->lock);
	r = (fifo->dataLength > 0);
	if(r){
		memcpy(data, bufferLocation(fifo, fifo->begin), fifo->elmtSize);
		if(doReadFlag){
			dequeueBuffer(fifo);
		}
	}
	releaseLock(&fifo->lock);
	return r;
}

int peekFIFO(FIFO *fifo, void *data){
	return _readFIFO(fifo, data, 0);
}

void readFIFO(FIFO *fifo, void *data){
	acquireSemaphore(fifo->semaphore); // wait until readable
	int r = _readFIFO(fifo, data, 1);
	assert(r == 1);
}

int readFIFONonBlock(FIFO *fifo, void *data){
	if(tryAcquireSemaphore(fifo->semaphore)){
		return _readFIFO(fifo, data, 1);
	}
	else{
		return 0;
	}
}

uintptr_t getElementSize(FIFO *fifo){
	return fifo->elmtSize;
}

uintptr_t getDataLength(FIFO *fifo){
	return fifo->dataLength;
}

FIFO *createFIFO(uintptr_t length, uintptr_t elementSize){
	assert((length & (length - 1)) == 0);
	FIFO *NEW(fifo);
	EXPECT(fifo != NULL);

	NEW_ARRAY(fifo->buffer, length * elementSize);
	EXPECT(fifo->buffer != NULL);
	fifo->lock = initialSpinlock;
	fifo->bufferLength = length;
	fifo->begin = 0;
	fifo->dataLength = 0;
	fifo->elmtSize = elementSize;
	fifo->semaphore = createSemaphore(0);
	EXPECT(fifo->semaphore != NULL);
	return fifo;
	//DELETE(fifo->semaphore);
	ON_ERROR;
	DELETE(fifo->buffer);
	ON_ERROR;
	DELETE(fifo);
	ON_ERROR;
	return NULL;
}

void deleteFIFO(FIFO *fifo){
	deleteSemaphore(fifo->semaphore);
	DELETE(fifo->buffer);
	DELETE(fifo);
}
