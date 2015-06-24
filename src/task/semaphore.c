#include"semaphore.h"
#include"task_private.h"
#include"interrupt/systemcall.h"
#include"memory/memory.h"
#include"multiprocessor/processorlocal.h"
#include"assembly/assembly.h"
#include"task.h"

struct BlockingTask{
	Task *task;
	volatile struct BlockingTask *prev, *next;
};
const Semaphore initialSemaphore = INITIAL_SEMAPHORE;

static void pushBlockingQueue(struct Semaphore *s, volatile BlockingTask *w, Task *t){
	w->task = t;
	if(s->lastWaiting == NULL){
		w->next = w->prev = NULL;
		s->firstWaiting = s->lastWaiting = w;
	}
	else{
		w->prev = s->lastWaiting;
		w->next = NULL;
		s->lastWaiting->next = w;
		s->lastWaiting = w;
	}
}

static Task *popBlockingQueue(struct Semaphore *s){
	volatile BlockingTask *b = s->firstWaiting;
	if(b == NULL){
		return NULL;
	}
	s->firstWaiting = b->next;
	if(s->firstWaiting == NULL){
		s->lastWaiting = NULL;
	}
	return b->task;
}

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

void acquireSemaphore(Semaphore *s){
	TaskManager *tm;
	BlockingTask *NEW(w);
assert(w!=NULL); // TODO: terminate?
	int acquired;
	int interruptFlag = getEFlags().bit.interrupt;
	if(interruptFlag){
		cli();
	}
	acquireLock(&s->lock);
	tm = processorLocalTaskManager();
	if(s->quota > 0){
		s->quota--;
		acquired = 1;
	}
	else{
		Task *current = currentTask(tm);
		suspend(current);
		pushBlockingQueue(s, w, current);
		acquired = 0;
	}
	releaseLock(&s->lock);
	if(acquired == 0){
		schedule(tm);
	}
	if(interruptFlag){
		sti();
	}
	DELETE(w);
}

void releaseSemaphore(Semaphore *s){
	Task *t;
	acquireLock(&s->lock);
	t = popBlockingQueue(s);
	if(t == NULL){
		s->quota++;
	}
	releaseLock(&s->lock);
	if(t != NULL){
		resume(t);
	}
}

void syscall_acquireSemaphore(Semaphore *s){
	systemCall1(SYSCALL_ACQUIRE_SEMAPHORE, (uintptr_t)s);
}
void syscall_releaseSemaphore(Semaphore *s){
	systemCall1(SYSCALL_RELEASE_SEMAPHORE, (uintptr_t)s);
}

void deleteSemaphore(Semaphore *s){
	s=(void*)s; // TODO:
}

void initSemaphore(SystemCallTable *systemCallTable){
	registerSystemCall(systemCallTable, SYSCALL_ACQUIRE_SEMAPHORE, _acquireSemaphore, 0);
	registerSystemCall(systemCallTable, SYSCALL_RELEASE_SEMAPHORE, _releaseSemaphore, 0);
}
