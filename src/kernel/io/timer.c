#include"ioservice.h"
#include"fifo.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"
#include"memory/memory.h"
#include"common.h"
#include"kernel.h"
#include"interrupt/handler.h"
#include"interrupt/controller/pic.h"
#include"assembly/assembly.h"

typedef struct TimerEvent{
	IORequest ior;
	uint64_t countdownTicks;
	// period = 0 for one-shot timer
	uint64_t tickPeriod;
	volatile int isSentToTask;
	Spinlock *lock;
	struct TimerEvent **prev, *next;
}TimerEvent;

struct TimerEventList{
	Spinlock lock;
	uint64_t currentTick;
	TimerEvent *head;
};

static void cancelTimerEvent(void *instance){
	TimerEvent *te = instance;
	acquireLock(te->lock);
	if(IS_IN_DQUEUE(te)){ // not expire
		REMOVE_FROM_DQUEUE(te);
	}
	releaseLock(te->lock);
	DELETE(te);
}

static int acceptTimerEvent(void *instance, __attribute__((__unused__)) uintptr_t *returnValues){
	TimerEvent *te = instance;
	if(te->tickPeriod == 0){ // not periodic
		DELETE(te);
	}
	else{
		acquireLock(te->lock);
		setCancellable(&te->ior, 1);
		pendIO(&te->ior);
		te->isSentToTask = 0;
		releaseLock(te->lock);
	}
	return 0;
}

static TimerEvent *createTimerEvent(uint64_t periodTicks){
	TimerEvent *NEW(te);
	if(te == NULL){
		return NULL;
	}
	initIORequest(&te->ior, te, cancelTimerEvent, acceptTimerEvent);
	te->countdownTicks = 0;
	te->tickPeriod = periodTicks;
	te->isSentToTask = 0;
	te->lock = NULL;
	te->prev = NULL;
	te->next = NULL;
	return te;
}

#define COUNTDOWN_TICK_MODULAR (((uint64_t)1) << 50)

static void addTimerEvent_noLock(TimerEventList* tel, uint64_t waitTicks, TimerEvent *te){
	TimerEvent **i;
	for(i = &tel->head; *i != NULL; i = &(*i)->next){
		uint64_t waitTicks2 = ((*i)->countdownTicks + COUNTDOWN_TICK_MODULAR - tel->currentTick) % COUNTDOWN_TICK_MODULAR;
		if(waitTicks <= waitTicks2){
			break;
		}
	}
	te->countdownTicks = (tel->currentTick + waitTicks) % COUNTDOWN_TICK_MODULAR;
	te->isSentToTask = 0;
	te->lock = &(tel->lock);
	ADD_TO_DQUEUE(te, i);
}

static void addTimerEvent(TimerEventList* tel, uint64_t waitTicks, TimerEvent *te){
	acquireLock(&tel->lock);
	addTimerEvent_noLock(tel, waitTicks, te);
	releaseLock(&tel->lock);
}

static void setAlarmHandler(InterruptParam *p){
	uint64_t millisecond = COMBINE64(SYSTEM_CALL_ARGUMENT_1(p), SYSTEM_CALL_ARGUMENT_0(p));
	uintptr_t isPeriodic = SYSTEM_CALL_ARGUMENT_2(p);
	// not check overflow
	uint64_t tick = (millisecond * TIMER_FREQUENCY) / 1000;

	EXPECT(tick < COUNTDOWN_TICK_MODULAR);
	if(tick == 0){
		tick++;
	}
	TimerEvent *te = createTimerEvent((isPeriodic? tick: 0));
	EXPECT(te != NULL);
	IORequest *ior = &te->ior;
	setCancellable(ior, 1);
	pendIO(ior);
	addTimerEvent(processorLocalTimer(), tick, te);
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)ior;
	return;
	ON_ERROR;
	ON_ERROR;
	SYSTEM_CALL_RETURN_VALUE_0(p) = IO_REQUEST_FAILURE;
}

static void handleTimerEvents(TimerEventList *tel){
	acquireLock(&tel->lock);
	TimerEvent **prev = &(tel->head);
	while(1){
		TimerEvent *curr = *prev;
		if(curr == NULL)
			break;
		if(curr->countdownTicks != tel->currentTick)
			break;
		REMOVE_FROM_DQUEUE(curr);
		if(curr->isSentToTask == 0){
			curr->isSentToTask = 1;
			completeIO(&curr->ior);
		}
#ifndef NDEBUG
		else{
			assert(curr->tickPeriod > 0);
			printk("warning: skip periodic timer event\n");
		}
#endif
		if(curr->tickPeriod > 0){
			// IMPROVE: addTimerEventList()
			addTimerEvent_noLock(tel, curr->tickPeriod, curr);
		}
	}
	tel->currentTick = (tel->currentTick + 1) % COUNTDOWN_TICK_MODULAR;
	releaseLock(&tel->lock);
}

static int chainedTimerHandler(const InterruptParam *p){
	handleTimerEvents((TimerEventList*)p->argument);
	schedule();
	return 1;
}

static void timerHandler(InterruptParam *p){
	// kprintf("interrupt #%d (timer), arg = %x\n", toChar(p.vector), p.argument);
	processorLocalPIC()->endOfInterrupt(p);
	chainedTimerHandler(p);
	//sti()
}

TimerEventList *createTimer(){
	TimerEventList *NEW(tel);
	tel->lock = initialSpinlock;
	tel->currentTick = 0;
	tel->head = NULL;
	return tel;
}

void setTimerHandler(TimerEventList *tel, InterruptVector *v){
	setHandler(v, timerHandler, (uintptr_t)tel);
}

int addTimerHandler(TimerEventList *tel, InterruptVector *v){
	return addHandler(v, chainedTimerHandler, (uintptr_t)tel);
}

void initTimer(SystemCallTable *systemCallTable){
	registerSystemCall(systemCallTable, SYSCALL_SET_ALARM, setAlarmHandler, 0);
}
