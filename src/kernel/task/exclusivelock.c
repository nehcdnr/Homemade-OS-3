#include"kernel.h"
#include"interrupt/systemcalltable.h"
#include"exclusivelock.h"
#include"task_private.h"
#include"memory/memory.h"
#include"multiprocessor/processorlocal.h"
#include"assembly/assembly.h"
#include"task.h"

typedef struct ExclusiveLock{
	void *instance;
	Spinlock lock;
	void (*pushLockQueue)(void *, Task*);
}ExclusiveLock;

static void initExclusiveLock(struct ExclusiveLock *exLock, void *instance){
	exLock->instance = instance;
	exLock->lock = initialSpinlock;
	exLock->pushLockQueue = NULL;
}

static void afterExLock(Task *t, uintptr_t exLockPtr){
	ExclusiveLock *exLock = (ExclusiveLock*)exLockPtr;
	assert(exLock->pushLockQueue != NULL);
	exLock->pushLockQueue(exLock->instance, t);
	exLock->pushLockQueue = NULL;
	releaseLock(&exLock->lock);
}

static int acquireExLock(ExclusiveLock *e, int (*acquire)(void*), void (*pushLockQueue)(void*, Task *), int doBlock){
	// cannot block when interrupt is off
	assert(getEFlags().bit.interrupt != 0);
	int interruptEnabled = getEFlags().bit.interrupt;
	// turn off interrupt to prevent sti in releaseLock in pushLockQueue
	if(interruptEnabled){
		cli();
	}
	acquireLock(&e->lock);
	assert(e->pushLockQueue == NULL);
	int acquired;
	if(acquire(e->instance)){
		releaseLock(&e->lock);
		acquired = 1;
	}
	else if(doBlock == 0){
		releaseLock(&e->lock);
		acquired = 0;
	}
	else{
		e->pushLockQueue = pushLockQueue;
		taskSwitch(afterExLock, (uintptr_t)e);
		acquired = 1;
	}
	if(interruptEnabled){
		sti();
	}
	return acquired;
}

static void releaseExLock(ExclusiveLock *e, void (*release)(void*, TaskQueue*)){
	TaskQueue q = INITIAL_TASK_QUEUE;
	acquireLock(&e->lock);
	release(e->instance, &q);
	releaseLock(&e->lock);
	while(1){
		Task *t = popQueue(&q);
		if(t == NULL){
			break;
		}
		resume(t);
	}
}

// Semaphore

struct Semaphore{
	volatile int quota;
	TaskQueue taskQueue;
	ExclusiveLock exLock;
};

static int _acquireSemaphore(void *inst){
	Semaphore *s = (Semaphore*)inst;
	if(s->quota >= 1){
		s->quota -= 1;
		return 1;
	}
	return 0;
}

static void _pushSemaphoreQueue(void *inst, Task *t){
	pushQueue(&((Semaphore*)inst)->taskQueue, t);
}

static void _releaseSemaphore(void *inst, TaskQueue *q){
	Semaphore *s = inst;
	Task *t = popQueue(&s->taskQueue);
	if(t == NULL){
		assert(s->quota + 1 != 0x7fffffff);
		s->quota += 1;
	}
	else{
		pushQueue(q, t);
	}
}

int tryAcquireSemaphore(Semaphore *s){
	return acquireExLock(&s->exLock, _acquireSemaphore, _pushSemaphoreQueue, 0);
}

void acquireSemaphore(Semaphore *s){
	acquireExLock(&s->exLock, _acquireSemaphore, _pushSemaphoreQueue, 1);
}

int tryAcquireAllSemaphore(Semaphore *s){
	int r;
	for(r = 0; 1; r++){
		if(tryAcquireSemaphore(s) == 0)
			return r;
	}
}

int acquireAllSemaphore(Semaphore *s){
	acquireSemaphore(s);
	return 1 + tryAcquireAllSemaphore(s);
}

void releaseSemaphore(Semaphore *s){
	releaseExLock(&s->exLock, _releaseSemaphore);
}

int getSemaphoreValue(Semaphore *s){
	int v;
	acquireLock(&s->exLock.lock);
	v = s->quota;
	releaseLock(&s->exLock.lock);
	return v;
}

Semaphore *createSemaphore(int initialValue){
	Semaphore *NEW(s);
	if(s == NULL)
		return NULL;
	s->quota = initialValue;
	s->taskQueue = initialTaskQueue;
	initExclusiveLock(&s->exLock, s);
	return s;
}

void deleteSemaphore(Semaphore *s){
	// do not check quota
	assert(s->taskQueue.head == NULL);
	DELETE(s);
}

// ReaderWriterLock

struct ReaderWriterLock{
	int writerFirst;
	int writerCount, readerCount;
	TaskQueue writerQueue, readerQueue;
	ExclusiveLock exLock;
};

ReaderWriterLock *createReaderWriterLock(int writerFirst){
	ReaderWriterLock *NEW(rwl);
	if(rwl == NULL)
		return NULL;
	rwl->writerFirst = writerFirst;
	rwl->writerCount =
	rwl->readerCount = 0;
	rwl->readerQueue = initialTaskQueue;
	rwl->writerQueue = initialTaskQueue;
	initExclusiveLock(&rwl->exLock, rwl);
	return rwl;
}

void deleteReaderWriterLock(ReaderWriterLock *rwl){
	assert(rwl->readerCount == 0 && rwl->writerCount == 0 &&
		IS_TASK_QUEUE_EMPTY(&rwl->readerQueue) && IS_TASK_QUEUE_EMPTY(&rwl->writerQueue));
	DELETE(rwl);
}

static int _acquireReaderLock(void *inst){
	ReaderWriterLock *rwl = inst;
	if(rwl->writerCount == 0 && (rwl->writerFirst == 0 || IS_TASK_QUEUE_EMPTY(&rwl->writerQueue))){
		rwl->readerCount++;
		return 1;
	}
	return 0;
}

static void _pushReaderQueue(void *inst, Task *t){
	pushQueue(&((ReaderWriterLock *)inst)->readerQueue, t);
}

void acquireReaderLock(ReaderWriterLock *rwl){
	acquireExLock(&rwl->exLock, _acquireReaderLock, _pushReaderQueue, 1);
}

static int _acquireWriterLock(void *inst){
	ReaderWriterLock *rwl = inst;
	if(rwl->writerCount == 0 && rwl->readerCount == 0){
		rwl->writerCount++;
		return 1;
	}
	return 0;
}

static void _pushWriterQueue(void *inst, Task *t){
	pushQueue(&((ReaderWriterLock *)inst)->writerQueue, t);
}

void acquireWriterLock(ReaderWriterLock *rwl){
	acquireExLock(&rwl->exLock, _acquireWriterLock, _pushWriterQueue, 1);
}

static void _releaseReaderWriterLock(void *instance, TaskQueue *q){
	ReaderWriterLock *rwl = instance;
	if(rwl->writerCount != 0){ // current task is writer
		rwl->writerCount--;
	}
	else/*if(rwl->readerCount != 0)*/{
		rwl->readerCount--;
	}
	if((rwl->writerFirst && IS_TASK_QUEUE_EMPTY(&rwl->writerQueue) == 0) ||
		IS_TASK_QUEUE_EMPTY(&rwl->readerQueue)){ // next writer
		Task *t = popQueue(&rwl->writerQueue);
		if(t != NULL){
			rwl->writerCount++;
			pushQueue(q, t);
		}
	}
	else{ // next readers
		while(1){
			Task *t = popQueue(&rwl->readerQueue);
			if(t == NULL){
				break;
			}
			rwl->readerCount++;
			pushQueue(q, t);
		}
	}
}

void releaseReaderWriterLock(ReaderWriterLock *rwl){
	releaseExLock(&rwl->exLock, _releaseReaderWriterLock);
}

#ifndef NDEBUG
#include"io/io.h"

static void testRWLock_r(void *rwlPtr){
	ReaderWriterLock *rwl = (*(ReaderWriterLock**)rwlPtr);

	acquireReaderLock(rwl);
	printk("test_r acquired\n");
	releaseReaderWriterLock(rwl);

	systemCall_terminate();
}

static void testRWLock_w(void *rwlPtr){
	ReaderWriterLock *rwl = (*(ReaderWriterLock**)rwlPtr);

	acquireWriterLock(rwl);
	printk("test_w acquired\n");
	releaseReaderWriterLock(rwl);

	systemCall_terminate();
}

void testRWLock(void);

void testRWLock(void){
	ReaderWriterLock *rl = createReaderWriterLock(0); // reader first
	ReaderWriterLock *wl = createReaderWriterLock(1); // writer first
	Task *task_r, *task_w;
	assert(wl != NULL && rl != NULL);
	acquireWriterLock(rl);
	acquireWriterLock(wl);

	task_r = createSharedMemoryTask(testRWLock_r, &rl, sizeof(rl), processorLocalTask());
	task_w = createSharedMemoryTask(testRWLock_w, &rl, sizeof(rl), processorLocalTask());
	assert(task_r != NULL && task_w != NULL);
	resume(task_r);
	resume(task_w);

	sleep(500);
	printk("test reader first lock\n");
	releaseReaderWriterLock(rl);
	sleep(500);

	task_r = createSharedMemoryTask(testRWLock_r, &wl, sizeof(wl), processorLocalTask());
	task_w = createSharedMemoryTask(testRWLock_w, &wl, sizeof(wl), processorLocalTask());
	assert(task_r != NULL && task_w != NULL);
	resume(task_r);
	resume(task_w);
	sleep(500);
	printk("test writer first lock\n");
	releaseReaderWriterLock(wl);
	sleep(500);

	acquireReaderLock(rl);
	printk("testRWLock_r acquired\n");
	task_r = createSharedMemoryTask(testRWLock_r, &rl, sizeof(rl), processorLocalTask());
	assert(task_r != NULL);
	resume(task_r);
	sleep(700);
	printk("testRWLock_r released\n");
	releaseReaderWriterLock(rl);

	deleteReaderWriterLock(rl);
	deleteReaderWriterLock(wl);
	printk("test rwlock ok\n");
	systemCall_terminate();
}
#endif
