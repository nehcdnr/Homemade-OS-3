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

typedef struct SpinlockBarrier{
	Spinlock lock;
	volatile int count;
}SpinlockBarrier;

void resetBarrier(SpinlockBarrier *b);
void waitAtBarrier(SpinlockBarrier *b, int threadCount);
