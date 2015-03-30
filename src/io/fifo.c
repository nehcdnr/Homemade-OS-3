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

void writeFIFO(FIFO *fifo, uintptr_t data){
	acquireLock(&fifo->lock);
	if(fifo->dataLength == fifo->bufferLength){
		printk("warning: fifo %x is full\n", fifo);
	}
	else{
		int i = ((fifo->begin + fifo->dataLength) & (fifo->bufferLength - 1));
		fifo->buffer[i] = data;
		fifo->dataLength++;
	}
	releaseLock(&fifo->lock);
}

static int peekOrReadFIFO(FIFO *fifo, uintptr_t *data, int readFlag){
	uintptr_t r;
	acquireLock(&fifo->lock);
	r = (fifo->dataLength != 0);
	if(r){
		*data = fifo->buffer[fifo->begin];
		if(readFlag){
			fifo->begin = ((fifo->begin + 1) & (fifo->bufferLength - 1));
			fifo->dataLength--;
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
