#include"multiprocessor/spinlock.h"

typedef struct Semaphore Semaphore;

// Semaphore must be in global kernel memory
Semaphore *createSemaphore(int initialValue);

void deleteSemaphore(Semaphore *s);
/*
void syscall_acquireSemaphore(Semaphore *s);
void syscall_releaseSemaphore(Semaphore *s);
*/
int tryAcquireSemaphore(Semaphore *s);
void acquireSemaphore(Semaphore *s);
// acquire semaphore at least once until the value reaches 0
int acquireAllSemaphore(Semaphore *s);
void releaseSemaphore(Semaphore *s);
int getSemaphoreValue(Semaphore *s);

typedef struct ReaderWriterLock ReaderWriterLock;
ReaderWriterLock *createReaderWriterLock(int writerFirst);
void deleteReaderWriterLock(ReaderWriterLock *rwl);

void acquireReaderLock(ReaderWriterLock *rwl);
void acquireWriterLock(ReaderWriterLock *rwl);
void releaseReaderWriterLock(ReaderWriterLock *rwl);
