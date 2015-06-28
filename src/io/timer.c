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

struct TimerEvent{
	IORequest this;
	uint64_t countDownTicks;
	Spinlock *lock;
	TimerEvent **prev, *next;
};

struct TimerEventList{
	Spinlock lock;
	TimerEvent *head;
};

void initIORequest(
	IORequest *this,
	void *instance,
	void (*h)(uintptr_t),
	uintptr_t a,
	void (*c)(struct IORequest*),
	void (*d)(struct IORequest*)
){
	this->ioRequest = instance;
	this->handle = h;
	this->arg = a;
	this->cancel = c;
	this->destroy = d;
}

static void cancelTimerEvent(IORequest *ior){
	TimerEvent *te = ior->timerEvent;
	acquireLock(te->lock);
	if(IS_IN_DQUEUE(te)){
		REMOVE_FROM_DQUEUE(te);
	}
	releaseLock(te->lock);
}

static void deleteTimerEvent(IORequest *ior){
	DELETE(ior->timerEvent);
}

IORequest *addTimerEvent(
	TimerEventList* tel, uint64_t waitTicks,
	void (*callback)(uintptr_t), uintptr_t arg
){
	TimerEvent *NEW(te);
	if(te == NULL){
		return NULL;
	}
	initIORequest(&te->this, te, callback, arg, cancelTimerEvent, deleteTimerEvent);
	te->countDownTicks = waitTicks;
	te->lock = &(tel->lock);
	acquireLock(&tel->lock);
	ADD_TO_DQUEUE(te, &(tel->head));
	releaseLock(&tel->lock);
	return &te->this;
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
			curr->this.handle(curr->this.arg);
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
