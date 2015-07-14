#include"semaphore.h"
#include"task_private.h"
#include"interrupt/systemcall.h"
#include"memory/memory.h"
#include"multiprocessor/processorlocal.h"
#include"assembly/assembly.h"
#include"task.h"

typedef struct Semaphore{
	volatile int quota;
	Spinlock lock;
	TaskQueue taskQueue;
}Semaphore;
/*
static void _acquireSemaphore(InterruptParam *p){
	Semaphore *s = (Semaphore*)SYSTEM_CALL_ARGUMENT_0(p);
	acquireSemaphore(s);
	sti();
}

static void _releaseSemaphore(InterruptParam *p){
	Semaphore *s = (Semaphore*)SYSTEM_CALL_ARGUMENT_0(p);
	releaseSemaphore(s);
	sti();
}
*/
static void pushSemaphoreQueue(Task *t, uintptr_t s_ptr){
	Semaphore *s = (Semaphore*)s_ptr;
	pushQueue(&s->taskQueue, t);
	releaseLock(&s->lock);
}

void acquireSemaphore(Semaphore *s){
	int interruptFlag = getEFlags().bit.interrupt;
	// turn off interrupt to prevent sti in releaseLock in pushSemaphoreQueue
	if(interruptFlag){
		cli();
	}
	acquireLock(&s->lock);
	if(s->quota > 0){
		s->quota--;
		releaseLock(&s->lock);
	}
	else{
		suspendCurrent(pushSemaphoreQueue, (uintptr_t)s);
	}
	if(interruptFlag){
		sti();
	}
}

void releaseSemaphore(Semaphore *s){
	Task *t;
	acquireLock(&s->lock);
	t = popQueue(&s->taskQueue);
	if(t == NULL){
		s->quota++;
	}
	releaseLock(&s->lock);
	if(t != NULL){
		resume(t);
	}
}
/*
void syscall_acquireSemaphore(Semaphore *s){
	systemCall1(SYSCALL_ACQUIRE_SEMAPHORE, (uintptr_t)s);
}
void syscall_releaseSemaphore(Semaphore *s){
	systemCall1(SYSCALL_RELEASE_SEMAPHORE, (uintptr_t)s);
}
*/
Semaphore *createSemaphore(){
	Semaphore *NEW(s);
	if(s == NULL)
		return NULL;
	s->lock = initialSpinlock;
	s->quota = 0;
	s->taskQueue = initialTaskQueue;
	return s;
}

void deleteSemaphore(Semaphore *s){
	s=(void*)s; // TODO:
}
/*
void initSemaphore(SystemCallTable *systemCallTable){
	registerSystemCall(systemCallTable, SYSCALL_ACQUIRE_SEMAPHORE, _acquireSemaphore, 0);
	registerSystemCall(systemCallTable, SYSCALL_RELEASE_SEMAPHORE, _releaseSemaphore, 0);
}
*/
