#include"task.h"
#include"task_private.h"
#include"semaphore.h"
#include"segment/segment.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"memory/memory_private.h"
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
	TaskMemoryManager *taskMemory;
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
	releaseLock(&globalQueue->lock); // see taskSwitch()
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
		contextSwitch(&tm->oldTask->esp0, tm->current->esp0, toCR3(getPageManager(tm->current->taskMemory)));
		// may go to startTask or return here
	}
	callAfterTaskSwitchFunc();
}
// see taskswitch.asm
void startTask(void);
void startTask(void){
	callAfterTaskSwitchFunc();
	sti();
}

void suspendCurrent(void (*afterTaskSwitchFunc)(Task*, uintptr_t), uintptr_t arg){
	taskSwitch(afterTaskSwitchFunc, arg);
}

void schedule(){
	taskSwitch(NULL, 0);
}

static int initV8086Memory(void){
	PageManager *p = getPageManager(processorLocalTask()->taskMemory);
	PhysicalAddress biosLow = {0x0}, // ~ 0x500: BIOS reserved
	v8086Stack = {V8086_STACK_BOTTOM}, // ~ 0x7c00: free
	v8086Text = {V8086_STACK_TOP}, // ~ 0x80000: free (OS)
	// biosHigh = {0x80000}, ~0x100000: reserved
	v8086End ={0x100000 + 0x10000};
//FIXME: buddy allocation
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
	uint32_t esp0, uint32_t espInterrupt, uintptr_t heapBegin, uintptr_t heapEnd,
	PageManager *pageManager, MemoryBlockManager *linearMemory, int priority
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
	t->taskMemory = createTaskMemory(pageManager, linearMemory, heapBegin, heapEnd);
	EXPECT(t->taskMemory != NULL);
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
static_assert(KERNEL_STACK_SIZE % PAGE_SIZE == 0);
static Task *createUserTask(
	void (*eip0)(void),	int priority
){
	const uintptr_t targetBlockManager = FLOOR(USER_LINEAR_END - maxBlockManagerSize, PAGE_SIZE);
	const uintptr_t targetPageTable = targetBlockManager - sizeOfPageTableSet;
	const uintptr_t targetESP0 = targetPageTable;
	const uintptr_t targetStackBottom = targetESP0 - KERNEL_STACK_SIZE;

	assert(minBlockManagerSize <= PAGE_SIZE);
	// 1. task PageManager
	PageManager *pageManager = createAndMapUserPageTable(
		targetStackBottom, targetBlockManager + PAGE_SIZE, targetPageTable);
	EXPECT(pageManager != NULL);
	// 2. task MemoryBlock
	int ok = mapPage_L(pageManager, (void*)targetBlockManager, PAGE_SIZE, KERNEL_PAGE);
	EXPECT(ok);
	MemoryBlockManager *mappedBlockManager =
		mapExistingPagesToKernel(pageManager, targetBlockManager, PAGE_SIZE, KERNEL_PAGE);
	EXPECT(mappedBlockManager != NULL);
	MemoryBlockManager *_mappedBlockManager = createMemoryBlockManager((uintptr_t)mappedBlockManager,
		maxBlockManagerSize, MIN_BLOCK_SIZE, MIN_BLOCK_SIZE, targetStackBottom);
	EXPECT(_mappedBlockManager == (void*)mappedBlockManager);
	// 3. task kernel stack
	ok = mapPage_L(pageManager, (void*)targetStackBottom, KERNEL_STACK_SIZE, KERNEL_PAGE);
	EXPECT(ok);
	void *mappedStackBottom = mapExistingPagesToKernel(pageManager, targetStackBottom, KERNEL_STACK_SIZE, KERNEL_PAGE);
	EXPECT(mappedStackBottom != NULL);
	// 4. create task
	EFlags eflags = getEFlags();
	eflags.bit.interrupt = 0;
	uintptr_t initialESP0 = targetESP0 -
	initTaskStack(eflags.value, (uint32_t)eip0, ((uintptr_t)mappedStackBottom) + (targetESP0 - targetStackBottom));
	Task *t = createTask(
		initialESP0, targetESP0 - 4,
		0, targetStackBottom,
		pageManager, (MemoryBlockManager*)targetBlockManager,
		priority
	);
	EXPECT(t != NULL);
	unmapKernelPages(mappedStackBottom);
	unmapKernelPages(mappedBlockManager);
	unmapUserPageTableSet(pageManager);
	// 5. see startTask
	return t;
	//DELETE(t);
	ON_ERROR;
	unmapKernelPages(mappedStackBottom);
	ON_ERROR;
	unmapPage_L(pageManager, (void*)targetStackBottom, KERNEL_STACK_SIZE);
	ON_ERROR;
	ON_ERROR;
	unmapKernelPages(mappedBlockManager);
	ON_ERROR;
	unmapPage_L(pageManager, (void*)targetBlockManager, PAGE_SIZE);
	ON_ERROR;
	deleteUserPageTable(pageManager);
	ON_ERROR;
	return NULL;
}

Task *createKernelTask(void (*eip0)(void), int priority){
	Task *t = createUserTask(eip0, priority);
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

LinearMemoryManager *getTaskLinearMemory(Task *t){
	return getLinearMemoryManager(t->taskMemory);
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
	tm->current = createTask(0, 0, 0, 0, kernelPageManager, NULL, NUMBER_OF_PRIORITIES - 1);
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

// system call

void putPendingIO(IORequest *ior){
	Task *t = ior->task;
	acquireLock(&t->ioListLock);
	ADD_TO_DQUEUE(ior, &t->pendingIOList);
	releaseLock(&t->ioListLock);
}

IORequest *waitIO(Task *t, IORequest *expected){
	// assume this is the only function acquiring ioSemaphore
	int v = getSemaphoreValue(t->ioSemaphore);
	while(v > 0){
		acquireSemaphore(t->ioSemaphore);
		v--;
	}
	while(1){
		IORequest *ior = NULL;
		acquireLock(&t->ioListLock);
		for(ior = t->finishedIOList; ior != NULL; ior = ior->next){
			if(expected == NULL || expected == ior){
				REMOVE_FROM_DQUEUE(ior);
				break;
			}
		}
		releaseLock(&t->ioListLock);
		if(ior != NULL){
			return ior;
		}
		acquireSemaphore(t->ioSemaphore);
	}
}

static void waitIOHandler(InterruptParam *p){
	sti();
	IORequest *ior = (IORequest*)SYSTEM_CALL_ARGUMENT_0(p);
	ior = waitIO(processorLocalTask(), ior);
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)ior;
	uintptr_t rv[SYSTEM_CALL_MAX_RETURN_COUNT - 1];
	int returnCount = ior->finish(ior, rv);
	switch(returnCount){
	case 5:
		SYSTEM_CALL_RETURN_VALUE_5(p) = rv[4];
	case 4:
		SYSTEM_CALL_RETURN_VALUE_4(p) = rv[3];
	case 3:
		SYSTEM_CALL_RETURN_VALUE_3(p) = rv[2];
	case 2:
		SYSTEM_CALL_RETURN_VALUE_2(p) = rv[1];
	case 1:
		SYSTEM_CALL_RETURN_VALUE_1(p) = rv[0];
	case 0:
		break;
	default:
		assert(0);
	}
}

uintptr_t systemCall_waitIO(uintptr_t expected){
	return systemCall2(SYSCALL_WAIT_IO, &expected);
}

uintptr_t systemCall_waitIOReturn(uintptr_t expected, int returnCount, ...){
	assert(returnCount >= 0 && returnCount <= SYSTEM_CALL_MAX_RETURN_COUNT - 1);
	va_list va;
	va_start(va, returnCount);
	uintptr_t ignoredReturnValue = 0;
	uintptr_t *returnValues[SYSTEM_CALL_MAX_RETURN_COUNT - 1];
	int i;
	for(i = 0; i < returnCount; i++){
		returnValues[i] = va_arg(va, uintptr_t*);
	}
	for(i = returnCount; i < (int)LENGTH_OF(returnValues); i++){
		returnValues[i] = &ignoredReturnValue;
	}
	(*returnValues[0]) = expected;
	uintptr_t rv0 = systemCall6(
		SYSCALL_WAIT_IO,
		returnValues[0],
		returnValues[1],
		returnValues[2],
		returnValues[3],
		returnValues[4]
	);
	va_end(va);
	return rv0;
}

void resumeTaskByIO(IORequest *ior){
	Task *t = ior->task;
	acquireLock(&t->ioListLock);
	REMOVE_FROM_DQUEUE(ior);
	ADD_TO_DQUEUE(ior, &(t->finishedIOList));
	releaseLock(&t->ioListLock);
	releaseSemaphore(t->ioSemaphore);
}

void initIORequest(
	IORequest *this,
	void *instance,
	HandleIORequest h,
	Task *t,
	int (*c)(IORequest*),
	FinishIORequest f
){
	this->ioRequest = instance;
	this->prev = NULL;
	this->next = NULL;
	this->handle = h;
	this->task = t;
	this->cancel = c;
	this->finish = f;
}

static void allocateHeapHandler(InterruptParam *p){
	sti();
	uintptr_t size = SYSTEM_CALL_ARGUMENT_0(p);
	PageAttribute attribute  =SYSTEM_CALL_ARGUMENT_1(p);
	void *ret = allocatePages(getLinearMemoryManager(processorLocalTask()->taskMemory), size, attribute);
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)ret;
}

void *systemCall_allocateHeap(uintptr_t size, PageAttribute attribute){
	uintptr_t address = systemCall3(SYSCALL_ALLOCATE_HEAP, &size, &attribute);
//assert(address != UINTPTR_NULL);
	return (void*)address;
}

static void releaseHeapHandler(InterruptParam *p){
	sti();
	uintptr_t address = SYSTEM_CALL_ARGUMENT_0(p);
	int ret = checkAndReleasePages(getLinearMemoryManager(processorLocalTask()->taskMemory), (void*)address);
	SYSTEM_CALL_RETURN_VALUE_0(p) = ret;
}

int systemCall_releaseHeap(void *address){
	uintptr_t address2 = (uintptr_t)address;
	uintptr_t ok = systemCall2(SYSCALL_RELEASE_HEAP, &address2);
	assert(ok); // TODO: remove this when we start working on user space tasks
	return (int)ok;
}

static void translatePageHandler(InterruptParam *p){
	uintptr_t address = SYSTEM_CALL_ARGUMENT_0(p);
	PhysicalAddress ret = checkAndTranslatePage(
		getLinearMemoryManager(processorLocalTask()->taskMemory), (void*)address);
	SYSTEM_CALL_RETURN_VALUE_0(p) = ret.value;
}

PhysicalAddress systemCall_translatePage(void *address){
	uintptr_t a = (uintptr_t)address;
	PhysicalAddress p = {systemCall2(SYSCALL_TRANSLATE_PAGE, &a)};
	return p;
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
	registerSystemCall(systemCallTable, SYSCALL_WAIT_IO, waitIOHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_ALLOCATE_HEAP, allocateHeapHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_RELEASE_HEAP, releaseHeapHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_TRANSLATE_PAGE, translatePageHandler, 0);
	//initSemaphore(systemCallTable);
}
