#include<std.h>

typedef struct Spinlock{
	volatile uint8_t acquirable;
	uint8_t interruptFlag;
}Spinlock;

#define INITIAL_SPINLOCK {acquirable: 1, interruptFlag: 0}
extern const Spinlock initialSpinlock;
#define NULL_SPINLOCK {acquirable: 2, interruptFlag: 0}
extern const Spinlock nullSpinlock;

int isAcquirable(Spinlock *spinlock);
int acquireLock(Spinlock *spinlock);
void releaseLock(Spinlock *spinlock);
