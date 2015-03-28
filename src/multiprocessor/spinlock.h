#include<std.h>

void initMultiprocessor(void);

typedef struct Spinlock{
	volatile uint32_t acquirable;
	int interruptFlag;
}Spinlock;

#define INITIAL_SPINLOCK {acquirable: 1, interruptFlag: 0}
extern const Spinlock initialSpinlock;
#define NULL_SPINLOCK {acquirable: 2, interruptFlag: 0}
extern const Spinlock nullSpinlock;

int isAcquirable(Spinlock *spinlock);
int acquireLock(Spinlock *spinlock);
void releaseLock(Spinlock *spinlock);
