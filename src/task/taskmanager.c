#include"task.h"
#include"task_private.h"
#include"semaphore.h"
#include"segment/segment.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"interrupt/handler.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"io/fifo.h"
#include"io/io.h"
#include"interrupt/systemcall.h"
#include"common.h"

typedef struct Task{
	// context
	// uint32_t ss;
	uint32_t esp0;
	uint32_t espInterrupt;
	// SegmentTable *ldt;
	TaskMemoryManager taskMemory;
	// queue data
	enum TaskState{
		// RUNNING,
		READY,
		SUSPENDED,
	}state;
	int priority;

	// system call
	SystemCallFunction taskDefinedSystemCall;
	uintptr_t taskDefinedArgument;

	Semaphore ioSemaphore;
	// ioRequest *pendingIOList;

	struct Task *next, *prev;
}Task;

PageManager *getPageManager(Task *t){
	return t->taskMemory.pageManager;
}

#define NUMBER_OF_PRIORITIES (4)
typedef struct TaskQueue{
	Task *head[NUMBER_OF_PRIORITIES];
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
		assert(p < NUMBER_OF_PRIORITIES);
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

void contextSwitch(uint32_t *oldTaskESP0, uint32_t newTaskESP0, uint32_t newCR3);

// assume interrupt is disabled
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
		contextSwitch(&oldTask->esp0, tm->current->esp0, toCR3(getPageManager(tm->current)));
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
}

static int initV8086Memory(void){
	PageManager *p;
	p = processorLocalTask()->taskMemory.pageManager;
	PhysicalAddress biosLow = {0x0}, // ~ 0x500: BIOS reserved
	v8086Stack = {V8086_STACK_BOTTOM}, // ~ 0x7c00: free
	v8086Text = {V8086_STACK_TOP}, // ~ 0x80000: free (OS)
	// biosHigh = {0x80000}, ~0x100000: reserved
	v8086End ={0x100000 + 0x10000};

	int ok = mapPage_LP(p, (void*)biosLow.value, biosLow, v8086Stack.value - biosLow.value, USER_WRITABLE_PAGE);
	EXPECT(ok);
	ok = mapPage_L(p, (void*)v8086Stack.value, v8086Text.value - v8086Stack.value, USER_WRITABLE_PAGE);
	EXPECT(ok);
	ok = mapPage_LP(p, (void*)v8086Text.value, v8086Text, v8086End.value - v8086Text.value, USER_WRITABLE_PAGE);
	EXPECT(ok);
	return 1;
	// unmapPage_LP(p, (void*)v8086Text.value, v8086End.value - v8086Text.value);
	ON_ERROR;
	unmapPage_L(p, (void*)v8086Stack.value, v8086Text.value - v8086Stack.value);
	ON_ERROR;
	unmapPage_LP(p, (void*)biosLow.value, v8086Stack.value - biosLow.value);
	ON_ERROR;
	return 0;
}

static void initV8086Registers(InterruptParam *p, uint16_t ip, uint16_t cs, uint16_t sp, uint16_t ss){
	p->regs.ds = p->regs.es = p->regs.fs = p->regs.gs = getDS();
	p->eip = ip;
	p->cs = cs;
	EFlags flags;
	flags.value = 0;
	flags.bit.reserve1 = 1;
	flags.bit.interrupt = 1;
	flags.bit.virtual8086 = 1;
	flags.bit.ioPrivilege = 0;
	p->eflags = flags;
	p->esp = sp;
	p->ss = ss;
	p->ds8086 = p->es8086 = p->fs8086 = p->gs8086 = 0;
}

// see interrupt/interruptentry.asm
void returnFromInterrupt(InterruptParam p);

int switchToVirtual8086Mode(void (*cs_ip)(void)){
	if(initV8086Memory() == 0){
		return 0;
	}
	uintptr_t ip = (((uintptr_t)cs_ip) & 0xf);
	uintptr_t cs = (((uintptr_t)cs_ip) >> 4);
	uintptr_t sp = V8086_STACK_TOP - 4;
	uintptr_t ss = ((sp < 0x10000? 0: CEIL(sp, 0x10) - 0x10000) >> 4);
	assert((cs << 4) + ip < (1<<20));
	InterruptParam p;
	initV8086Registers(&p, ip, cs, sp, ss);
	returnFromInterrupt(p);
	panic("switchToVirtual8086Mode");
	return 0;
}

static void undefinedSystemCall(__attribute__((__unused__)) InterruptParam *p){
	printk("task = %x", processorLocalTask());
	panic("undefined Task system call");
}

uint32_t initTaskStack(uint32_t eFlags, uint32_t eip, uint32_t esp0);

static Task *createTask(
	uint32_t esp0, uint32_t espInterrupt, uintptr_t userStackTop, uintptr_t userHeapBottom,
	PageManager *pageTable, int priority
){
	Task *NEW(t);
	EXPECT(t != NULL);
	t->esp0 = esp0;
	t->espInterrupt = espInterrupt;
	int initTaskMemoryOK = initTaskMemory(&(t->taskMemory), pageTable, userStackTop, userHeapBottom);
	EXPECT(initTaskMemoryOK != 0);
	t->state = SUSPENDED;
	t->priority = priority;
	t->taskDefinedSystemCall = undefinedSystemCall;
	t->taskDefinedArgument = 0;
	t->ioSemaphore = initialSemaphore;
	t->next =
	t->prev = NULL;

	return t;

	ON_ERROR;
	DELETE(t);
	ON_ERROR;
	return NULL;
}

#define KERNEL_STACK_SIZE ((size_t)8192)
static Task *createUserTask(
	void (*eip0)(void),
	uintptr_t userHeapBottom,
	int priority
){
	const uintptr_t targetESP0 = USER_LINEAR_END - sizeOfPageTableSet;
	const uintptr_t targetStackBottom = targetESP0 - KERNEL_STACK_SIZE;
	// 1. task page table
	PageManager *pageManager = createAndMapUserPageTable(targetESP0);
	EXPECT(pageManager != NULL);

	// 2. kernel stack for task
	// TODO: use current page table, not kernel
	int allocateStackOK = mapPage_L(pageManager, (void*)targetStackBottom, KERNEL_STACK_SIZE, KERNEL_PAGE);
	EXPECT(allocateStackOK != 0);
	void *mappedStackBottom = mapKernelPagesFromExisting(pageManager, targetStackBottom, KERNEL_STACK_SIZE, KERNEL_PAGE);
	EXPECT(mappedStackBottom != NULL);
	// 3. create task
	EFlags eflags = getEFlags();
	eflags.bit.interrupt = 0;
	uintptr_t initialESP0 = targetESP0 -
	initTaskStack(eflags.value, (uint32_t)eip0, ((uintptr_t)mappedStackBottom) + (targetESP0 - targetStackBottom));
	Task *t = createTask(initialESP0, targetESP0 - 4,
	(uintptr_t)targetStackBottom, userHeapBottom,
	pageManager, priority);
	EXPECT(t != NULL);
	unmapKernelPage((void*)mappedStackBottom);
	unmapUserPageTableSet(pageManager);
	return t;
	//DELETE(t);
	ON_ERROR;
	unmapKernelPage((void*)mappedStackBottom);
	ON_ERROR;
	// TODO: use current page table, not kernel
	unmapPage_L(pageManager, (void*)targetStackBottom, KERNEL_STACK_SIZE);
	ON_ERROR;
	deleteUserPageTable(pageManager);
	ON_ERROR;
	return NULL;
}

Task *createKernelTask(void (*eip0)(void)){
	Task *t = createUserTask(eip0, (2 << 20), 0);
	return t;
}

void setTaskSystemCall(Task *t, SystemCallFunction f, uintptr_t a){
	t->taskDefinedSystemCall = f;
	t->taskDefinedArgument = a;
}

void resume(/*TaskManager *tm, */Task *t){
	acquireLock(&globalQueueLock);
	assert(t->state == SUSPENDED);
	t->state = READY;
	pushQueue(globalQueue, t);
	releaseLock(&globalQueueLock);
}

void suspend(Task *t){
	t->state = SUSPENDED;
}

Task *currentTask(TaskManager *tm){
	return tm->current;
}

static void syscallTaskDefined(InterruptParam *p){
	uintptr_t oldArgument = p->argument;
	Task *t = processorLocalTask();
	p->argument = t->taskDefinedArgument;
	t->taskDefinedSystemCall(p);
	p->argument = oldArgument;
	// schedule(p->processorLocal->taskManager);
}

TaskManager *createTaskManager(SegmentTable *gdt){
	assert(globalQueue != NULL);
	TaskManager *NEW(tm);
	// create a task for this, eip and esp are irrelevant
	tm->current = createTask(0, 0, 0, 0, kernelPageManager, 3);
	if(tm->current == NULL){
		panic("cannot initialize bootstrap task");
	}
	// do not put into the queue because the task is running
	tm->current->state = READY;
	tm->gdt = gdt;
	//pushQueue(globalQueue, createTask(b, testTask, 0));
	return tm;
}

static void syscallAllocateHeap(InterruptParam *p){
	size_t size = SYSTEM_CALL_ARGUMENT_0(p);
	PageAttribute attribute = SYSTEM_CALL_ARGUMENT_1(p);
	Task *t = processorLocalTask();
	int result = extendHeap(&(t->taskMemory), size, attribute);
	SYSTEM_CALL_RETURN_VALUE_0(p) = result;
}

static void syscallReleaseHeap(InterruptParam *p){
	size_t size = SYSTEM_CALL_ARGUMENT_0(p);
	Task *t = processorLocalTask();
	int result = shrinkHeap(&(t->taskMemory), size);
	SYSTEM_CALL_RETURN_VALUE_0(p) = result;
}


// system call
static void wakeupTask(uintptr_t arg){
	Task *t = (Task*)arg;
	releaseSemaphore(&t->ioSemaphore);
}

int sleep(uint64_t millisecond){
	if(millisecond > 1000000000 * (uint64_t)1000){
		return 0;
	}
	Task *t = processorLocalTask();
	IORequest *te = addTimerEvent(
		processorLocalTimer(), (millisecond * TIMER_FREQUENCY) / 1000, wakeupTask,
		(uintptr_t)t
	);
	if(te == NULL){ // TODO: insufficient memory. terminate task
		return 0;
	}
	acquireSemaphore(&t->ioSemaphore);
	te->destroy(te);
	return 1;
}

void initTaskManagement(SystemCallTable *systemCallTable){
	NEW(globalQueue);
	globalQueueLock = initialSpinlock;
	int t;
	for(t = 0; t < NUMBER_OF_PRIORITIES; t++){
		globalQueue->head[t] = NULL;
	}
	registerSystemCall(systemCallTable, SYSCALL_TASK_DEFINED, syscallTaskDefined, 0);
	registerSystemCall(systemCallTable, SYSCALL_ALLOCATE_HEAP, syscallAllocateHeap, 0);
	registerSystemCall(systemCallTable, SYSCALL_RELEASE_HEAP, syscallReleaseHeap, 0);

	initSemaphore(systemCallTable);
}
