#ifndef SPINLOCK_H_INCLUDED
#define SPINLOCK_H_INCLUDED

#include<std.h>

// spinlock

typedef struct Spinlock{
	volatile uint8_t acquirable;
	volatile uint8_t interruptFlag;
}Spinlock;

#define INITIAL_SPINLOCK {acquirable: 1, interruptFlag: 0}
extern const Spinlock initialSpinlock;
#define NULL_SPINLOCK {acquirable: 2, interruptFlag: 0}
extern const Spinlock nullSpinlock;

int isAcquirable(Spinlock *spinlock);
int acquireLock(Spinlock *spinlock);
void releaseLock(Spinlock *spinlock);

// barrier

typedef struct Barrier{
	volatile uint32_t count;
}Barrier;

#define INITIAL_BARRIER {0}
const Barrier initialBarrier;

void resetBarrier(Barrier *b);
void addBarrier(Barrier *b);
void addAndWaitAtBarrier(Barrier *b, uint32_t threadCount);

#endif
