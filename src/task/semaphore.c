#include"semaphore.h"
#include"task_private.h"
#include"interrupt/systemcall.h"
#include"memory/memory.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"
#include"assembly/assembly.h"
#include"task.h"

typedef struct BlockingTask{
	Task *task;
	volatile struct BlockingTask *prev, *next;
}BlockingTask;

struct Semaphore{
	volatile int quota;
	Spinlock lock;
	volatile BlockingTask *firstWaiting, *lastWaiting;
};

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
	assert(w != NULL);
	int acquired;
	cli();
	acquireLock(&s->lock);
	tm = getProcessorLocal()->taskManager;
	if(s->quota > 0){
		s->quota--;
		acquired = 1;
	}
	else{
		pushBlockingQueue(s, w, suspendCurrent(tm));
		acquired = 0;
	}
	releaseLock(&s->lock);
	if(acquired == 0){
		schedule(getProcessorLocal()->taskManager);
	}
	sti();
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

Semaphore *createSemaphore(unsigned initialQuota){
	Semaphore *NEW(s);
	s->quota = initialQuota;
	s->lock = initialSpinlock;
	s->firstWaiting = NULL;
	s->lastWaiting = NULL;
	return s;
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
