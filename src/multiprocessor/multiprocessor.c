
#include"common.h"
#include"assembly/assembly.h"
#include"spinlock.h"
#include"memory/memory.h"

struct Spinlock{
	volatile uint32_t acquirable;
	int interruptFlag;
};

Spinlock *createSpinlock(MemoryManager *m){
	Spinlock *NEW(s, m);
	s->acquirable = 1;
	s->interruptFlag = 0;
	return s;
}

int isAcquirable(Spinlock *spinlock){
	if(spinlock == nullSpinlock)
		return 1;
	return spinlock->acquirable;
}

int acquireLock(Spinlock *spinlock){
	if(spinlock == nullSpinlock){
		return 0;
	}
	spinlock->interruptFlag = (getEFlags() & EFLAGS_IF);
	int tryCount = 0;
	while(1){
		cli();
		int acquired = xchg(&spinlock->acquirable, 0);
		if(acquired)
			return tryCount;
		if(spinlock->interruptFlag)
			sti();
		do{
			tryCount++;
			__asm__("pause\n");
		}while(spinlock->acquirable == 0);
	}
}

void releaseLock(Spinlock *spinlock){
	if(spinlock == nullSpinlock){
		return;
	}
	xchg(&spinlock->acquirable, 1);
	if(spinlock->interruptFlag){
		sti();
	}
}

void initMultiprocessor(){
	#ifndef NDEBUG
	{
		Spinlock s = {1, 0};
		acquireLock(&s);
		assert(isAcquirable(&s) == 0);
		releaseLock(&s);
		assert(isAcquirable(&s) == 1);
	}
	#endif
}
