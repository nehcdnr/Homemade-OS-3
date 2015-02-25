
void initMultiprocessor(void);

typedef struct Spinlock Spinlock;
typedef struct MemoryManager MemoryManager;
Spinlock *createSpinlock(MemoryManager *m);
int isAcquirable(Spinlock *spinlock);
void acquireLock(Spinlock *spinlock);
void releaseLock(Spinlock *spinlock);
