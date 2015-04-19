#include"task.h"
#include"task_private.h"
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
	PageDirectory *userPageTable;
	// PageDirectory *kernelPageTable;
	// queue data
	enum TaskState{
		// RUNNING,
		READY,
		SUSPENDED,
	}state;
	int priority;

	SystemCallFunction taskDefinedSystemCall;
	uintptr_t taskDefinedArgument;

	struct Task *next, *prev;
}Task;

#define NUMBER_OF_PRIORITY (4)
typedef struct TaskQueue{
	Task *head[NUMBER_OF_PRIORITY];
}TaskQueue;
static TaskQueue *globalQueue = NULL;
static Spinlock globalQueueLock = INITIAL_SPINLOCK;
static int totalBlockCount = 0;

struct TaskManager{
	Task *current;
	SegmentTable *gdt;
	// TaskQueue *taskQueue;
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
	int tryCount = acquireLock(&globalQueueLock);
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
	releaseLock(&globalQueueLock);
}
// see taskswitch.asm
void startTask(void);
void startTask(void){
	releaseLock(&globalQueueLock); // after contextSwitch in schedule
	sti(); // acquireLock
	// return to eip assigned in initTaskStack
	// TODO: application loader
}

//void startUserMode(PrivilegeChangeInterruptParam p);
void startVirtual8086Mode(InterruptParam p);

void switchToVirtual8086Mode(void (*cs_ip)(void), uintptr_t ss_sp){
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
	p.regs.ds = p.regs.es = p.regs.fs = p.regs.gs = getDS();
	p.ds8086 = p.es8086 = p.fs8086 = p.gs8086 = 0;
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

static void undefinedSystemCall(__attribute__((__unused__)) InterruptParam *p){
	printk("task = %x", getProcessorLocal()->taskManager->current);
	panic("undefined Task system call");
}

uint32_t initTaskStack(uint32_t eFlags, uint32_t eip0, uint32_t esp0);

static Task *createTask(
	void (*eip0)(void),
	int priority
){
	uintptr_t esp0 = (uintptr_t)allocate(MIN_BLOCK_SIZE);
	assert(esp0 % MIN_BLOCK_SIZE == 0);// TODO: page?
	esp0 += MIN_BLOCK_SIZE;
	esp0 -= sizeof(Task);
	esp0 -= esp0 % 4;
	Task *t = (Task*)esp0;
	t->userPageTable = NULL; // TODO
	t->state = SUSPENDED;
	t->priority = priority;
	EFlags eflags = getEFlags();
	eflags.bit.interrupt = 0;
	t->espInterrupt = esp0;
	t->esp0 = initTaskStack(eflags.value, (uint32_t)eip0, esp0);
	t->taskDefinedSystemCall = undefinedSystemCall;
	t->taskDefinedArgument = 0;
	t->next =
	t->prev = NULL;
	return t;
}

Task *createKernelTask(void (*eip0)(void)){
	Task *t = createTask(eip0, 0);
	return t;
}

void setTaskSystemCall(Task *t, SystemCallFunction f, uintptr_t a){
	t->taskDefinedSystemCall = f;
	t->taskDefinedArgument = a;
}

void resume(/*TaskManager *tm, */Task *t){
	acquireLock(&globalQueueLock);
	if(t->state == SUSPENDED){
		t->state = READY;
		pushQueue(globalQueue, t);
	}
	releaseLock(&globalQueueLock);
}

Task *suspendCurrent(TaskManager *tm){
	tm->current->state = SUSPENDED;
	return tm->current;
}

static void syscallTaskDefined(InterruptParam *p){
	uintptr_t oldArgument = p->argument;
	p->argument = getProcessorLocal()->taskManager->current->taskDefinedArgument;
	getProcessorLocal()->taskManager->current->taskDefinedSystemCall(p);
	p->argument = oldArgument;
	// schedule(p->processorLocal->taskManager);
}

TaskManager *createTaskManager(SegmentTable *gdt){
	assert(globalQueue != NULL);
	TaskManager *NEW(tm);
	// create a task for this, eip and esp are irrelevant
	tm->current = createTask(testTask, 0);
	tm->current->state = READY;
	tm->gdt = gdt;
	//pushQueue(globalQueue, createTask(b, testTask, 0));
	return tm;
}
/*
static void syscallAllocatePage(InterruptParam *p){
	uintptr_t linearAddress = SYSTEM_CALL_ARGUMENT_0(p);
	uintptr_t size = SYSTEM_CALL_ATGUMENT_1(p);
	uintptr_t physicalAddress =
	Task *c = getProcessorLocal()->taskManager->current;
	PageDirectory *pd = c->userPageTable;

}

static void syscallFreePage(InterruptParam *p){

}
*/
void initTaskManagement(SystemCallTable *systemCallTable){
	NEW(globalQueue);
	globalQueueLock = initialSpinlock;
	int t;
	for(t = 0; t < NUMBER_OF_PRIORITY; t++){
		globalQueue->head[t] = NULL;
	}
	registerSystemCall(systemCallTable, SYSCALL_TASK_DEFINED, syscallTaskDefined, 0);
	/*
	registerSystemCall(systemCallTable, SYSCALL_ALLOCATE_PAGE, syscallAllocatePage, 0);
	registerSystemCall(systemCallTable, SYSCALL_FREE_PAGE, syscallFreePage, 0);
	*/
	initSemaphore(systemCallTable);
}
