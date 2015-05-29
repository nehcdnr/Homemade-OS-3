
#include"common.h"
#include"assembly/assembly.h"
#include"spinlock.h"

#define NOT_ACQUIRABLE (0)
#define ACQUIRABLE (1)
#define IGNORED (2)

const Spinlock initialSpinlock = INITIAL_SPINLOCK;
const Spinlock nullSpinlock = NULL_SPINLOCK;

int isAcquirable(Spinlock *spinlock){
	if(spinlock->acquirable == IGNORED)
		return 1;
	return spinlock->acquirable != NOT_ACQUIRABLE;
}

int acquireLock(Spinlock *spinlock){
	if(spinlock->acquirable == IGNORED){
		return 0;
	}
	spinlock->interruptFlag = (getEFlags().bit.interrupt);
	int tryCount = 0;
	while(1){
		cli();
		int acquired = xchg8(&spinlock->acquirable, NOT_ACQUIRABLE);
		if(acquired == ACQUIRABLE)
			return tryCount;
		if(spinlock->interruptFlag)
			sti();
		do{
			tryCount++;
			pause();
		}while(spinlock->acquirable == NOT_ACQUIRABLE);
	}
}

void releaseLock(Spinlock *spinlock){
	if(spinlock->acquirable == IGNORED){
		return;
	}
	xchg8(&spinlock->acquirable, ACQUIRABLE);
	if(spinlock->interruptFlag){
		sti();
	}
}
/*
#ifndef NDEBUG
void testSpinlock(void){
	Spinlock s = initialSpinlock;
	acquireLock(&s);
	assert(isAcquirable(&s) == 0);
	releaseLock(&s);
	assert(isAcquirable(&s) == 1);
}
#endif
*/

// barrier

void resetBarrier(SpinlockBarrier *b){
	b->lock = initialSpinlock;
	b->count = 0;
}

void waitAtBarrier(SpinlockBarrier *b, int threadCount){
	acquireLock(&b->lock);
	b->count++;
	releaseLock(&b->lock);
	int continueFlag = 1;
	while(continueFlag){
		acquireLock(&b->lock);
		if(b->count >= threadCount){
			continueFlag = 0;
		}
		releaseLock(&b->lock);
		pause();
	}
}

