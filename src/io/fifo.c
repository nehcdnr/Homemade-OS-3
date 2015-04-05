#include"fifo.h"
#include<common.h>
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"

struct FIFO{
	int begin, dataLength;
	int bufferLength;
	Spinlock lock;
	uintptr_t *buffer;
};

static void dequeue1(FIFO *fifo){
	fifo->begin = ((fifo->begin + 1) & (fifo->bufferLength - 1));
	fifo->dataLength--;
}

static int _writeFIFO(FIFO *fifo, uintptr_t data, int throwOldestFlag){
	int r = 1;
	acquireLock(&fifo->lock);
	if(fifo->dataLength == fifo->bufferLength){
		printk("warning: fifo %x is full\n", fifo);
		if(throwOldestFlag){
			dequeue1(fifo);
			r = 1;
		}
		else{
			r = 0;
		}
	}
	if(r == 1){
		int i = ((fifo->begin + fifo->dataLength) & (fifo->bufferLength - 1));
		fifo->buffer[i] = data;
		fifo->dataLength++;
	}
	releaseLock(&fifo->lock);
	return r;
}

int writeFIFO(FIFO *fifo, uintptr_t data){
	return _writeFIFO(fifo, data, 0);
}

int overwriteFIFO(FIFO *fifo, uintptr_t data){
	return _writeFIFO(fifo, data, 1);
}

static int peekOrReadFIFO(FIFO *fifo, uintptr_t *data, int readFlag){
	uintptr_t r;
	acquireLock(&fifo->lock);
	r = (fifo->dataLength != 0);
	if(r){
		*data = fifo->buffer[fifo->begin];
		if(readFlag){
			dequeue1(fifo);
		}
	}
	releaseLock(&fifo->lock);
	return r;
}

int peekFIFO(FIFO *fifo, uintptr_t *data){
	return peekOrReadFIFO(fifo, data, 0);
}

int readFIFO(FIFO *fifo, uintptr_t *data){
	return peekOrReadFIFO(fifo, data, 1);
}

FIFO *createFIFO(int length){
	assert((length & (length - 1)) == 0);
	FIFO *NEW(fifo);
	NEW_ARRAY(fifo->buffer, length);
	fifo->lock = initialSpinlock;
	fifo->bufferLength = length;
	fifo->begin = 0;
	fifo->dataLength = 0;
	return fifo;
}
