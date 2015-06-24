#include"multiprocessor/spinlock.h"

typedef struct BlockingTask BlockingTask;
typedef struct Semaphore{
	volatile int quota;
	Spinlock lock;
	volatile BlockingTask *firstWaiting, *lastWaiting;
}Semaphore;

#define INITIAL_SEMAPHORE {0, INITIAL_SPINLOCK, NULL, NULL}
extern const Semaphore initialSemaphore;

void deleteSemaphore(Semaphore *s);
// system call
void syscall_acquireSemaphore(Semaphore *s);
void syscall_releaseSemaphore(Semaphore *s);
// interrupt handler functions
void acquireSemaphore(Semaphore *s);
void releaseSemaphore(Semaphore *s);
