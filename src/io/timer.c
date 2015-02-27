#include"io.h"
#include"memory/memory.h"
#include"common.h"
#include"interrupt/handler.h"
#include"assembly/assembly.h"

typedef struct TimerEvent TimerEvent;
struct TimerEvent{
	int waitTicks;
	void (*callback)(uintptr_t);
	uintptr_t arg;
	volatile TimerEvent *nextEvent;
};

struct TimerEventList{
	volatile TimerEvent *head;
};

static void addTimerEvent(TimerEventList* tel, volatile TimerEvent *te){
	int intFlag = (getEFlags() & EFLAGS_IF);
	if(intFlag)
	cli();
	if(te->waitTicks <= 0){
		te->callback(te->arg);
	}
	else{
		te->nextEvent = tel->head;
		tel->head = te;
	}
	sti();
}

static void kernelWakeup(uintptr_t isTimedout){
	(*(volatile int*)isTimedout) = 1;
}

void kernelSleep(TimerEventList *tel, unsigned millisecond){
	volatile TimerEvent te;
	volatile int isTimedout = 0;
	te.waitTicks = DIV_CEIL(TIMER_FREQUENCY * millisecond, 1000);
	te.callback = kernelWakeup;
	te.arg = (uintptr_t)&isTimedout;
	addTimerEvent(tel, &te);
	while(isTimedout == 0){
		hlt();
	}
}

static void timerHandler(InterruptParam p){
	kprintf("interrupt #%d (timer), arg = %x\n", toChar(p.vector), p.argument);
	endOfInterrupt(p.vector);
	volatile TimerEvent *volatile *prev = &(((TimerEventList*)p.argument)->head);
	while(*prev != NULL){
		volatile TimerEvent *curr = *prev;
		curr->waitTicks--;
		if(curr->waitTicks <= 0){
			*prev = curr->nextEvent;
			curr->callback(curr->arg);
		}
		else{
			prev = &(curr->nextEvent);
		}
	}
	sti();
}

TimerEventList *createTimer(MemoryManager *m){
	TimerEventList *tel = allocate(m, sizeof(TimerEventList));
	tel->head = NULL;
	return tel;
}

void replaceTimerHandler(TimerEventList *tel, InterruptVector *v){
	InterruptHandler h = timerHandler;
	uintptr_t a = (uintptr_t)tel;
	replaceHandler(v, &h, &a);
}
