typedef struct Semaphore Semaphore;

Semaphore *createSemaphore(unsigned initialQuota);
void deleteSemaphore(Semaphore *s);
// system call
void syscall_acquireSemaphore(Semaphore *s);
void syscall_releaseSemaphore(Semaphore *s);
// interrupt handler functions
void acquireSemaphore(Semaphore *s);
void releaseSemaphore(Semaphore *s);
