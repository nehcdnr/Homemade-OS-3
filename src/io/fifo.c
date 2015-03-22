#include"fifo.h"
#include<common.h>
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"

struct FIFO{
	int begin, dataLength;
	int bufferLength;
	Spinlock *lock;
	uintptr_t *buffer;
};

void writeFIFO(FIFO *fifo, uintptr_t data){
	acquireLock(fifo->lock);
	if(fifo->dataLength == fifo->bufferLength){
		printk("warning: fifo %x is full\n", fifo);
	}
	else{
		int i = ((fifo->begin + fifo->dataLength) & (fifo->bufferLength - 1));
		fifo->buffer[i] = data;
		fifo->dataLength++;
	}
	releaseLock(fifo->lock);
}

int readFIFO(FIFO *fifo, uintptr_t *data){
	uintptr_t r;
	acquireLock(fifo->lock);
	r = (fifo->dataLength != 0);
	if(r){
		*data = fifo->buffer[fifo->begin];
		fifo->begin = ((fifo->begin + 1) & (fifo->bufferLength - 1));
		fifo->dataLength--;
	}
	releaseLock(fifo->lock);
	return r;
}

FIFO *createFIFO(MemoryManager *m, int length){
	assert((length & (length - 1)) == 0);
	FIFO *NEW(fifo, m);
	NEW_ARRAY(fifo->buffer, m, length);
	fifo->lock = createSpinlock(m);
	fifo->bufferLength = length;
	fifo->begin = 0;
	fifo->dataLength = 0;
	return fifo;
}
