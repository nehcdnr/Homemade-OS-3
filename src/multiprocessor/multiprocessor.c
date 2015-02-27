
#include"common.h"
#include"assembly/assembly.h"
#include"spinlock.h"
#include"memory/memory.h"

struct Spinlock{
	volatile uint32_t acquirable;
	int interruptFlag;
};

Spinlock *createSpinlock(MemoryManager *m){
	Spinlock *s = allocate(m, sizeof(Spinlock));
	s->acquirable = 1;
	s->interruptFlag = 0;
	return s;
}

int isAcquirable(Spinlock *spinlock){
	return spinlock->acquirable;
}

void acquireLock(Spinlock *spinlock){
	spinlock->interruptFlag = (getEFlags() & EFLAGS_IF);
	int tryCount = 0;
	while(1){
		cli();
		int acquired = xchg(&spinlock->acquirable, 0);
		if(acquired)
			break;
		if(spinlock->interruptFlag)
			sti();
		do{
			tryCount++;
			__asm__("pause\n");
		}while(spinlock->acquirable == 0);
	}
}

void releaseLock(Spinlock *spinlock){
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
