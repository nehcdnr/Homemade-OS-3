#include"fifo.h"
#include<common.h>
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"
#include"task/exclusivelock.h"

struct FIFO{
	uintptr_t begin, dataSize, bufferSize;
	Spinlock lock;
	volatile uint8_t *buffer;

	Semaphore *semaphore;
	uintptr_t elmtSize;
};

static void dequeueBuffer(FIFO *fifo, uintptr_t n){
	fifo->begin = ((fifo->begin + n) & (fifo->bufferSize - 1));
	fifo->dataSize -= n;
}

static int _writeFIFO(FIFO *fifo, uint8_t *data, uintptr_t dataSize, int throwOldestFlag){
	int sizeChangeFlag = 0, writeFlag = 1;
	acquireLock(&fifo->lock);
	sizeChangeFlag = (dataSize <= fifo->bufferSize - fifo->dataSize);
	if(sizeChangeFlag == 0){
		//printk("warning: fifo %x is full\n", fifo);
		if(throwOldestFlag){
			dequeueBuffer(fifo, dataSize);
			writeFlag = 1;
		}
		else{
			writeFlag = 0;
		}
	}
	if(writeFlag){
		uintptr_t i;
		for(i = 0; i < dataSize; i++){
			int j = ((fifo->begin + fifo->dataSize + i) & (fifo->bufferSize - 1));
			fifo->buffer[j] = data[i];
		}
		fifo->dataSize += dataSize;
	}
	releaseLock(&fifo->lock);
	return sizeChangeFlag;
}

int writeFIFO(FIFO *fifo, void *data){
	int r = _writeFIFO(fifo, data, fifo->elmtSize, 0);
	if(r){
		releaseSemaphore(fifo->semaphore);
	}
	return r;
}

int overwriteFIFO(FIFO *fifo, void *data){
	if(_writeFIFO(fifo, data, fifo->elmtSize, 1)){
		releaseSemaphore(fifo->semaphore);
	}
	return 1;
}

static int _readFIFO(FIFO *fifo, uint8_t *data, uintptr_t dataSize, int doReadFlag){
	uintptr_t r;
	acquireLock(&fifo->lock);
	r = (fifo->dataSize >= dataSize);
	if(r){
		uintptr_t i;
		for(i = 0; i < dataSize; i++){
			uintptr_t j = ((fifo->begin + i) & (fifo->bufferSize - 1));
			data[i] = fifo->buffer[j];
		}
		if(doReadFlag){
			dequeueBuffer(fifo, dataSize);
		}
	}
	releaseLock(&fifo->lock);
	return r;
}

int peekFIFO(FIFO *fifo, void *data){
	return _readFIFO(fifo, data, fifo->elmtSize, 0);
}

void readFIFO(FIFO *fifo, void *data){
	acquireSemaphore(fifo->semaphore); // wait until readable
	int r = _readFIFO(fifo, data, fifo->elmtSize, 1);
	assert(r == 1);
}

int readFIFONonBlock(FIFO *fifo, void *data){
	if(tryAcquireSemaphore(fifo->semaphore)){
		return _readFIFO(fifo, data, fifo->elmtSize, 1);
	}
	else{
		return 0;
	}
}

uintptr_t getElementSize(FIFO *fifo){
	//acquireLock(&fifo->lock);
	return fifo->elmtSize;
	//releaseLock(&fifo->lock);
}

FIFO *createFIFO(uintptr_t length, uintptr_t elementSize){
	assert((length & (length - 1)) == 0);
	FIFO *NEW(fifo);
	EXPECT(fifo != NULL);

	NEW_ARRAY(fifo->buffer, length);
	EXPECT(fifo->buffer != NULL);
	fifo->lock = initialSpinlock;
	fifo->bufferSize = length;
	fifo->begin = 0;
	fifo->dataSize = 0;
	fifo->elmtSize = elementSize;
	fifo->semaphore = createSemaphore(0);
	EXPECT(fifo->semaphore != NULL);
	return fifo;
	//DELETE(fifo->semaphore);
	ON_ERROR;
	DELETE((void*)fifo->buffer);
	ON_ERROR;
	DELETE(fifo);
	ON_ERROR;
	return NULL;
}
