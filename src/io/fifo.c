#include"fifo.h"
#include<common.h>
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"
#include"task/semaphore.h"

struct FIFO{
	int begin, dataLength;
	int bufferLength;
	Spinlock lock;
	volatile uintptr_t *buffer;
	Semaphore *semaphore;
};

static void dequeue1(FIFO *fifo){
	fifo->begin = ((fifo->begin + 1) & (fifo->bufferLength - 1));
	fifo->dataLength--;
}

static int _writeFIFO(FIFO *fifo, uintptr_t data, int throwOldestFlag){
	int sizeChangeFlag = 0, writeFlag = 1;
	acquireLock(&fifo->lock);
	sizeChangeFlag = (fifo->dataLength < fifo->bufferLength);
	if(sizeChangeFlag == 0){
		printk("warning: fifo %x is full\n", fifo);
		if(throwOldestFlag){
			dequeue1(fifo);
			writeFlag = 1;
		}
		else{
			writeFlag = 0;
		}
	}
	if(writeFlag){
		int i = ((fifo->begin + fifo->dataLength) & (fifo->bufferLength - 1));
		fifo->buffer[i] = data;
		fifo->dataLength++;
	}
	releaseLock(&fifo->lock);
	return sizeChangeFlag;
}

int writeFIFO(FIFO *fifo, uintptr_t data){
	int r = _writeFIFO(fifo, data, 0);
	if(r){
		releaseSemaphore(fifo->semaphore);
	}
	return r;
}

int overwriteFIFO(FIFO *fifo, uintptr_t data){
	if(_writeFIFO(fifo, data, 1)){
		releaseSemaphore(fifo->semaphore);
	}
	return 1;
}

static int _readFIFO(FIFO *fifo, uintptr_t *data, int doReadFlag){
	uintptr_t r;
	acquireLock(&fifo->lock);
	r = (fifo->dataLength != 0);
	if(r){
		*data = fifo->buffer[fifo->begin];
		if(doReadFlag){
			dequeue1(fifo);

		}
	}
	releaseLock(&fifo->lock);
	return r;
}

int peekFIFO(FIFO *fifo, uintptr_t *data){
	return _readFIFO(fifo, data, 0);
}

int readFIFO(FIFO *fifo, uintptr_t *data){
	acquireSemaphore(fifo->semaphore); // wait until readable
	return _readFIFO(fifo, data, 1);
}

FIFO *createFIFO(int length){
	assert((length & (length - 1)) == 0);
	FIFO *NEW(fifo);
	NEW_ARRAY(fifo->buffer, length);
	fifo->lock = initialSpinlock;
	fifo->bufferLength = length;
	fifo->begin = 0;
	fifo->dataLength = 0;
	fifo->semaphore = createSemaphore(0);
	return fifo;
}
