
#include"common.h"
#include"assembly/assembly.h"
#include"spinlock.h"

#define NOT_ACQUIRABLE (0)
#define ACQUIRABLE (1)
#define IGNORED (2)

const Spinlock initialSpinlock = {acquirable: ACQUIRABLE, interruptFlag: 0};
const Spinlock nullSpinlock = {acquirable: IGNORED, interruptFlag: 0};

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
		int acquired = xchg(&spinlock->acquirable, NOT_ACQUIRABLE);
		if(acquired == ACQUIRABLE)
			return tryCount;
		if(spinlock->interruptFlag)
			sti();
		do{
			tryCount++;
			__asm__("pause\n");
		}while(spinlock->acquirable == NOT_ACQUIRABLE);
	}
}

void releaseLock(Spinlock *spinlock){
	if(spinlock->acquirable == IGNORED){
		return;
	}
	xchg(&spinlock->acquirable, ACQUIRABLE);
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
