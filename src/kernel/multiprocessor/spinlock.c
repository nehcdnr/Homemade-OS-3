
#include"common.h"
#include"kernel.h"
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
	unsigned interruptEnabled = getEFlags().bit.interrupt;

	int tryCount = 0;
	while(1){
		cli();
		int acquired = xchg8(&spinlock->acquirable, NOT_ACQUIRABLE);
		if(acquired == ACQUIRABLE){
			spinlock->interruptFlag = interruptEnabled;
			return tryCount;
		}
		if(interruptEnabled){
			sti();
		}
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
	assert(spinlock->acquirable == NOT_ACQUIRABLE);
	assert(getEFlags().bit.interrupt == 0);
	int interruptEnabled = spinlock->interruptFlag;
	xchg8(&spinlock->acquirable, ACQUIRABLE);
	if(interruptEnabled){
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

const Barrier initialBarrier = INITIAL_BARRIER;

void resetBarrier(Barrier *b){
	b->count = 0;
}

void addBarrier(Barrier *b){
	lock_add32(&b->count, 1);
}

void addAndWaitAtBarrier(Barrier *b, uint32_t threadCount){
	lock_add32(&b->count, 1);
	while(lock_cmpxchg32(&b->count, threadCount, threadCount) != threadCount){
		pause();
	}
}

