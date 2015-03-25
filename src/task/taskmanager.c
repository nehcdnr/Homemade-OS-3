#include"task.h"
#include"segment/segment.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"memory/page.h"
#include"interrupt/handler.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/systemcall.h"
#include"common.h"

typedef struct Task{
	// context
	// uint32_t ss;
	uint32_t esp0;
	uint32_t espInterrupt;
	// SegmentTable *ldt;
	PageDirectory *pageTable;
	// queue data
	enum TaskState{
		// RUNNING,
		READY,
		SUSPENDED,
	}state;
	int priority;

	SystemCallFunction taskDefinedSystemCall;

	struct Task *next, *prev;
}Task;

#define NUMBER_OF_PRIORITY (4)
typedef struct TaskQueue{
	Task *head[NUMBER_OF_PRIORITY];
}TaskQueue;
static TaskQueue *globalQueue = NULL;
static Spinlock *globalQueueLock = NULL;
static int totalBlockCount = 0;

struct TaskManager{
	Task *current;
	SegmentTable *gdt;
	// TaskQueue *taskQueue;
	BlockManager *blockManager;
};

static void pushQueue(struct TaskQueue *q, Task *t){
	const int p = t->priority;
	t->state = READY;
	if(q->head[p] == NULL){
		q->head[p] =
		t->next =
		t->prev = t;
	}
	else{
		t->next = q->head[p];
		t->prev = q->head[p]->prev;
		t->next->prev = t;
		t->prev->next = t;
	}
}

static Task *popQueue(struct TaskQueue *q){
	int p;
	for(p = 0; q->head[p] == NULL; p++){
		assert(p < NUMBER_OF_PRIORITY);
	}
	struct Task *t = q->head[p];
	if(t->next == t/* && t->prev == t*/){
		q->head[p] = NULL;
	}
	else{
		q->head[p] = t->next;
		t->next->prev = t->prev;
		t->prev->next = t->next;
	}
	t->next = t->prev = NULL;
	return t;
}

void contextSwitch(uint32_t *oldTaskESP0, uint32_t newTaskESP0);

void schedule(TaskManager *tm){
	struct Task *oldTask = tm->current;
	int tryCount = acquireLock(globalQueueLock);
	totalBlockCount += tryCount;
	if(oldTask->state == READY){
		pushQueue(globalQueue, oldTask);
	}
	tm->current = popQueue(globalQueue);
	setTSSKernelStack(tm->gdt, tm->current->espInterrupt);
	if(oldTask != tm->current){
		contextSwitch(&oldTask->esp0, tm->current->esp0);
		// may go to startTask or return here
	}
	releaseLock(globalQueueLock);
}
// see taskswitch.asm
void startTask(void);
void startTask(void){
	releaseLock(globalQueueLock); // after contextSwitch in schedule
	sti(); // acquireLock
	// return to eip assigned in initTaskStack
	// TODO: application loader
}

//void startUserMode(PrivilegeChangeInterruptParam p);
void startVirtual8086Mode(InterruptParam p);

void startVirtual8086Task(void (*cs_ip)(void), uintptr_t ss_sp){
	uintptr_t cs_ip2 = (uintptr_t)cs_ip;
	InterruptParam p;
	assert(cs_ip2 < (1<<20));
	assert(ss_sp < (1<<20));

	p.eip = (cs_ip2 & 0xf);
	p.cs = (cs_ip2 >> 4);

	if(ss_sp < 0x10000){
		p.ss = 0;
		p.esp = ss_sp;
	}
	else{
		p.ss = ((ss_sp - 0x10000) >> 4) + 1;
		p.esp = ss_sp - (p.ss << 4);
	}
	EFlags flags;
	flags.value = 0;
	flags.bit.reserve1 = 1;
	flags.bit.interrupt = 1;
	flags.bit.virtual8086 = 1;
	flags.bit.ioPrivilege = 0;
	p.eflags = flags;
	startVirtual8086Mode(p);
	panic("startVirtual8086UserTask");
}

static void testTask(void){
	int a=0;
	while(1){
		printk(" %d %d %d\n",cpuid_getInitialAPICID(),totalBlockCount,a++);
		hlt();
	}
}

static void undefinedSystemCall(InterruptParam *p){
	printk("task = %x", p->processorLocal->taskManager->current);
	panic("undefined Task system call");
}

uint32_t initTaskStack(uint32_t eFlags, uint32_t eip0, uint32_t esp0);

static Task *createTask(
	BlockManager *p,
	void (*eip0)(void),
	int priority
){
	uintptr_t esp0 = (uintptr_t)allocateBlock(p, MIN_BLOCK_SIZE);
	esp0 += MIN_BLOCK_SIZE;
	esp0 -= sizeof(Task);
	esp0 -= esp0 % 4;
	Task *t = (Task*)esp0;
	t->pageTable = NULL; // TODO
	t->state = SUSPENDED;
	t->priority = priority;
	EFlags eflags = getEFlags();
	eflags.bit.interrupt = 0;
	t->espInterrupt = esp0;
	t->esp0 = initTaskStack(eflags.value, (uint32_t)eip0, esp0);
	t->taskDefinedSystemCall = undefinedSystemCall;
	t->next =
	t->prev = NULL;
	return t;
}

Task *createKernelTask(TaskManager *tm, void (*eip0)(void)){
	Task *t = createTask(tm->blockManager, eip0, 0);
	return t;
}

void setTaskSystemCall(Task *t, SystemCallFunction f){
	t->taskDefinedSystemCall = f;
}

void resume(/*TaskManager *tm, */Task *t){
	acquireLock(globalQueueLock);
	if(t->state == SUSPENDED){
		t->state = READY;
		pushQueue(globalQueue, t);
	}
	releaseLock(globalQueueLock);
}

static void syscallSuspend(InterruptParam *p){
	// not need to lock current task
	p->processorLocal->taskManager->current->state = SUSPENDED;
	schedule(p->processorLocal->taskManager);
}

static void syscallTaskDefined(InterruptParam *p){
	p->processorLocal->taskManager->current->taskDefinedSystemCall(p);
	// schedule(p->processorLocal->taskManager);
}

TaskManager *createTaskManager(
	MemoryManager *m,
	SystemCallTable *systemCallTable,
	BlockManager *b,
	SegmentTable *gdt
){
	static int needInit = 1;
	TaskManager *NEW(tm, m);
	// create a task for this, eip and esp are irrelevant
	tm->current = createTask(b, testTask, 0);
	tm->current->state = READY;
	tm->blockManager = b;
	tm->gdt = gdt;
	if(needInit){
		needInit = 0;
		NEW(globalQueue, m);
		globalQueueLock = createSpinlock(m);
		int t;
		for(t = 0; t < NUMBER_OF_PRIORITY; t++){
			globalQueue->head[t] = NULL;
		}
		registerSystemCall(systemCallTable, SYSCALL_SUSPEND, syscallSuspend);
		registerSystemCall(systemCallTable, SYSCALL_TASK_DEFINED, syscallTaskDefined);
	}
	//pushQueue(globalQueue, createTask(b, testTask, 0));
	return tm;
}
