#include"io.h"
#include"fifo.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"
#include"memory/memory.h"
#include"common.h"
#include"interrupt/handler.h"
#include"interrupt/controller/pic.h"
#include"assembly/assembly.h"

typedef struct TimerEvent{
	IORequest this;
	uint64_t countDownTicks;
	Spinlock *lock;
	struct TimerEvent **prev, *next;
}TimerEvent;

struct TimerEventList{
	Spinlock lock;
	TimerEvent *head;
};

void initIORequest(
	IORequest *this,
	void *instance,
	IORequestHandler h,
	uintptr_t a,
	int (*c)(struct IORequest*),
	void (*d)(struct IORequest*)
){
	this->ioRequest = instance;
	this->prev = NULL;
	this->next = NULL;
	this->handle = h;
	this->arg = a;
	this->cancel = c;
	this->destroy = d;
}

static int cancelTimerEvent(IORequest *ior){
	TimerEvent *te = ior->timerEvent;
	acquireLock(te->lock);
	if(IS_IN_DQUEUE(te)){
		REMOVE_FROM_DQUEUE(te);
	}
	releaseLock(te->lock);
	return 1;
}

static void deleteTimerEvent(IORequest *ior){
	DELETE(ior->timerEvent);
}

static TimerEvent *createTimerEvent(
	IORequestHandler callback, uintptr_t arg
){
	TimerEvent *NEW(te);
	if(te == NULL){
		return NULL;
	}
	initIORequest(&te->this, te, callback, arg, cancelTimerEvent, deleteTimerEvent);
	te->prev = NULL;
	te->next = NULL;
	return te;
}

static void addTimerEvent(TimerEventList* tel, uint64_t waitTicks, TimerEvent *te){
	te->countDownTicks = waitTicks;
	te->lock = &(tel->lock);
	acquireLock(&tel->lock);
	ADD_TO_DQUEUE(te, &(tel->head));
	releaseLock(&tel->lock);
}

// TODO:systemCall_sleep(uint64_t millisecond);
int sleep(uint64_t millisecond){
	if(millisecond > 1000000000 * (uint64_t)1000){
		return 0;
	}
	Task *t = processorLocalTask();
	TimerEvent *te = createTimerEvent(resumeTaskByIO, (uintptr_t)t);
	if(te == NULL){ // TODO: insufficient memory
		return 0;
	}
	IORequest *ior = &te->this;
	putPendingIO(t, ior);
	addTimerEvent(processorLocalTimer(), (millisecond * TIMER_FREQUENCY) / 1000, te);
	IORequest *te2 = waitIO(t); // TODO: if there are other pending requests?
	assert(te2 == ior);
	ior->destroy(ior);
	return 1;
}

static void handleTimerEvents(TimerEventList *tel){
	acquireLock(&tel->lock);
	TimerEvent **prev = &(tel->head);
	while(*prev != NULL){
		TimerEvent *curr = *prev;
		if(curr->countDownTicks > 0){
			curr->countDownTicks--;
			prev = &((*prev)->next);
		}
		else{
			REMOVE_FROM_DQUEUE(curr);
			//releaseLock(&tel->lock);
			curr->this.handle(&curr->this);
			//acquireLock(&tel->lock);
		}
	}
	releaseLock(&tel->lock);
}

static void timerHandler(InterruptParam *p){
	// kprintf("interrupt #%d (timer), arg = %x\n", toChar(p.vector), p.argument);
	processorLocalPIC()->endOfInterrupt(p);
	handleTimerEvents((TimerEventList*)p->argument);
	schedule();
	sti();
}

TimerEventList *createTimer(){
	TimerEventList *NEW(tel);
	tel->lock = initialSpinlock;
	tel->head = NULL;
	return tel;
}

void setTimerHandler(TimerEventList *tel, InterruptVector *v){
	setHandler(v, timerHandler, (uintptr_t)tel);
}
