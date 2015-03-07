
void initMultiprocessor(void);

typedef struct Spinlock Spinlock;
typedef struct MemoryManager MemoryManager;
Spinlock *createSpinlock(MemoryManager *m);
int isAcquirable(Spinlock *spinlock);
int acquireLock(Spinlock *spinlock);
void releaseLock(Spinlock *spinlock);

#define nullSpinlock ((Spinlock*)0)
// extern Spinlock *const nullSpinlock;
