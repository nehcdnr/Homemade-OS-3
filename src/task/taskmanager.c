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

	Spinlock ioListLock;
	Semaphore *ioSemaphore; // length of finishedIOLIst
	IORequest *pendingIOList, *finishedIOList;

	struct Task *next, *prev;
}Task;

PageManager *getPageManager(Task *t){
	return t->taskMemory.pageManager;
}

#define NUMBER_OF_PRIORITIES (4)

typedef struct TaskPriorityQueue{
	Spinlock lock;
	TaskQueue taskQueue[NUMBER_OF_PRIORITIES];
}TaskPriorityQueue;

static TaskPriorityQueue *globalQueue = NULL;

struct TaskManager{
	Task *current;
	SegmentTable *gdt;

	Task *oldTask; // see switchCurrent()
	void (*afterTaskSwitchFunc)(Task*, uintptr_t);
	uintptr_t afterTaskSwitchArg;
};


const TaskQueue initialTaskQueue = INITIAL_TASK_QUEUE;

void pushQueue(TaskQueue *q, Task *t){
	if(q->head == NULL){
		q->head =
		t->next =
		t->prev = t;
	}
	else{
		t->next = q->head;
		t->prev = q->head->prev;
		t->next->prev = t;
		t->prev->next = t;
	}
}

Task *popQueue(TaskQueue *q){
	struct Task *t;
	t = q->head;
	if(t == NULL){
		return NULL;
	}
	if(t->next == t/* && t->prev == t*/){
		assert(t->prev == t);
		q->head = NULL;
	}
	else{
		q->head = t->next;
		t->next->prev = t->prev;
		t->prev->next = t->next;
	}
	t->next = t->prev = NULL;
	return t;
}

static void pushPriorityQueue(TaskPriorityQueue *q, Task *t){
	pushQueue(q->taskQueue + t->priority, t);
}

static Task *popPriorityQueue(TaskPriorityQueue *q){
	int p;
	for(p = 0; 1; p++){
		assert(p < NUMBER_OF_PRIORITIES);
		Task *t = popQueue(q->taskQueue + p);
		if(t != NULL)
			return t;
	}
}

void contextSwitch(uint32_t *oldTaskESP0, uint32_t newTaskESP0, uint32_t newCR3);

static void callAfterTaskSwitchFunc(void){
	releaseLock(&globalQueue->lock); // FIXME: see taskSwitch()
	TaskManager *tm = processorLocalTaskManager();

	if(tm->afterTaskSwitchFunc != NULL){
		tm->afterTaskSwitchFunc(tm->oldTask, tm->afterTaskSwitchArg);
		tm->afterTaskSwitchFunc = NULL;
	}
	tm->afterTaskSwitchArg = 0;
	tm->oldTask = NULL;
}

// assume interrupt is disabled
static void taskSwitch(void (*func)(Task *t, uintptr_t), uintptr_t arg){
	TaskManager *tm = processorLocalTaskManager();
	// putting into suspendQueue and switching to another task have to be atomic
	tm->afterTaskSwitchFunc = func;
	tm->afterTaskSwitchArg = arg;
	tm->oldTask = tm->current;
	acquireLock(&globalQueue->lock);
	if(func == NULL){
		pushPriorityQueue(globalQueue, tm->oldTask);
	}
	else{
		tm->oldTask->state = SUSPENDED;
	}
	tm->current = popPriorityQueue(globalQueue);
	// if releaseLock() before contextSwitch(), the stack sometimes becomes corrupted
	//releaseLock(&readyQueue->lock);
	setTSSKernelStack(tm->gdt, tm->current->espInterrupt);
	if(tm->current != tm->oldTask){// otherwise, esp0 will be wrong value
		contextSwitch(&tm->oldTask->esp0, tm->current->esp0, toCR3(getPageManager(tm->current)));
		// may go to startTask or return here
	}
	callAfterTaskSwitchFunc();
}
// see taskswitch.asm
void startTask(void);
void startTask(void){
	callAfterTaskSwitchFunc();
	sti();
	// return to eip assigned in initTaskStack
}

void suspendCurrent(void (*afterTaskSwitchFunc)(Task*, uintptr_t), uintptr_t arg){
	taskSwitch(afterTaskSwitchFunc, arg);
}

void schedule(){
	taskSwitch(NULL, 0);
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

	t->ioSemaphore = createSemaphore();
	EXPECT(t->ioSemaphore != NULL);
	t->ioListLock = initialSpinlock;
	t->pendingIOList = NULL;
	t->finishedIOList = NULL;
	int initTaskMemoryOK = initTaskMemory(&(t->taskMemory), pageTable, userStackTop, userHeapBottom);
	EXPECT(initTaskMemoryOK != 0);
	t->state = SUSPENDED;
	t->priority = priority;
	t->taskDefinedSystemCall = undefinedSystemCall;
	t->taskDefinedArgument = 0;
	t->next =
	t->prev = NULL;

	return t;

	ON_ERROR;
	DELETE(t->ioSemaphore);
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

Task *createKernelTask(void (*eip0)(void), int priority){
	Task *t = createUserTask(eip0, (2 << 20), priority);
	return t;
}

void setTaskSystemCall(Task *t, SystemCallFunction f, uintptr_t a){
	t->taskDefinedSystemCall = f;
	t->taskDefinedArgument = a;
}

void resume(/*TaskManager *tm, */Task *t){
	assert(t->state == SUSPENDED);
	t->state = READY;
	acquireLock(&globalQueue->lock);
	pushPriorityQueue(globalQueue, t);
	releaseLock(&globalQueue->lock);
}

Task *currentTask(TaskManager *tm){
	return tm->current;
}

static void taskDefinedHandler(InterruptParam *p){
	uintptr_t oldArgument = p->argument;
	Task *t = processorLocalTask();
	p->argument = t->taskDefinedArgument;
	t->taskDefinedSystemCall(p);
	p->argument = oldArgument;
	// schedule(p->processorLocal->taskManager);
}

TaskManager *createTaskManager(SegmentTable *gdt){
	assert(globalQueue != NULL);
	// each processor needs an idle task
	// create a task for current running bootstrap thread. not need to initialize eip and esp
	TaskManager *NEW(tm);
	if(tm == NULL){
		panic("cannot initialize bootstrap task");
	}
	tm->current = createTask(0, 0, 0, 0, kernelPageManager, NUMBER_OF_PRIORITIES - 1);
	if(tm->current == NULL){
		panic("cannot initialize bootstrap task");
	}
	// do not put into the queue because the task is running
	tm->current->state = READY;
	tm->gdt = gdt;
	tm->oldTask = NULL;
	tm->afterTaskSwitchFunc = NULL;
	tm->afterTaskSwitchArg = 0;
	return tm;
}

static void allocateHeapHandler(InterruptParam *p){
	size_t size = SYSTEM_CALL_ARGUMENT_0(p);
	PageAttribute attribute = SYSTEM_CALL_ARGUMENT_1(p);
	Task *t = processorLocalTask();
	int result = extendHeap(&(t->taskMemory), size, attribute);
	SYSTEM_CALL_RETURN_VALUE_0(p) = result;
}

static void releaseHeapHandler(InterruptParam *p){
	size_t size = SYSTEM_CALL_ARGUMENT_0(p);
	Task *t = processorLocalTask();
	int result = shrinkHeap(&(t->taskMemory), size);
	SYSTEM_CALL_RETURN_VALUE_0(p) = result;
}

// system call

void putPendingIO(Task *t, IORequest *ior){
	acquireLock(&t->ioListLock);
#ifndef NDEBUG
	if(t->finishedIOList != NULL){
		printk("warning: task %x has finished IORequest but not released yet.\n", t);
	}
#endif
	ADD_TO_DQUEUE(ior, &t->pendingIOList);
	releaseLock(&t->ioListLock);
}

IORequest *waitIO(Task *t){
	IORequest *ior;
	acquireSemaphore(t->ioSemaphore);
	acquireLock(&t->ioListLock);
	ior = t->finishedIOList;
	REMOVE_FROM_DQUEUE(ior);
	releaseLock(&t->ioListLock);
	assert(ior != NULL);
	return ior;
}

static void waitIOHandler(__attribute__((__unused__)) InterruptParam *p){
	sti();
	IORequest *ior = waitIO(processorLocalTask());
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)ior;
	ior->destroy(ior);
}

uintptr_t systemCall_waitIO(void){
	return systemCall0(SYSCALL_WAIT_IO);
}

void resumeTaskByIO(IORequest *ior){
	Task *t = (Task*)ior->arg;
	acquireLock(&t->ioListLock);
	REMOVE_FROM_DQUEUE(ior);
	ADD_TO_DQUEUE(ior, &(t->finishedIOList));
	releaseLock(&t->ioListLock);
	releaseSemaphore(t->ioSemaphore);
}

void initTaskManagement(SystemCallTable *systemCallTable){
	NEW(globalQueue);
	if(globalQueue == NULL){
		panic("cannot initialize task management");
	}
	int p;
	globalQueue->lock = initialSpinlock;
	for(p = 0; p < NUMBER_OF_PRIORITIES; p++){
		globalQueue->taskQueue[p] = initialTaskQueue;
	}

	registerSystemCall(systemCallTable, SYSCALL_TASK_DEFINED, taskDefinedHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_ALLOCATE_HEAP, allocateHeapHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_RELEASE_HEAP, releaseHeapHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_WAIT_IO, waitIOHandler, 0);
	//initSemaphore(systemCallTable);
}
