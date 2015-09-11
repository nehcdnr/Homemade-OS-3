#include"task.h"
#include"task_private.h"
#include"semaphore.h"
#include"assembly/assembly.h"
#include"memory/segment.h"
#include"memory/memory.h"
#include"memory/memory_private.h"
#include"interrupt/handler.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"io/fifo.h"
#include"io/io.h"
#include"interrupt/systemcall.h"
#include"common.h"

typedef struct TaskMemoryManager{
	LinearMemoryManager linear;
	Spinlock lock;
	int referenceCount;
}TaskMemoryManager;

static TaskMemoryManager *kernelTaskMemory = NULL;

static int addReference(TaskMemoryManager *m, int value){
	acquireLock(&m->lock);
	m->referenceCount += value;
	int r = m->referenceCount;
	releaseLock(&m->lock);
	return r;
}

static TaskMemoryManager *createTaskMemory(PageManager *p, LinearMemoryBlockManager *b){
	TaskMemoryManager *NEW(m);
	if(m == NULL)
		return NULL;
	assert(p != NULL);
	m->linear.page = p;
	m->linear.linear = b;
	m->linear.physical = kernelLinear->physical;
	m->lock = initialSpinlock;
	m->referenceCount = 0;
	return m;
}

static void deleteTaskMemory(TaskMemoryManager *m){
	assert(m->referenceCount == 0 && isAcquirable(&m->lock));
	DELETE(m);
}
enum TaskState{
	// RUNNING,
	READY,
	SUSPENDED,
};
typedef struct Task{
	// kernel space memory
	// uint32_t ss;
	// SegmentTable *ldt;
	uint32_t esp0;
	uint32_t espInterrupt;
	void *stackBottom;

	// user space memory
	TaskMemoryManager *taskMemory;

	// scheduling
	enum TaskState state;
	int priority;

	// system call
	SystemCallFunction taskDefinedSystemCall;
	uintptr_t taskDefinedArgument;

	// blocking
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
void taskSwitch(void (*func)(Task*, uintptr_t), uintptr_t arg){
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
		contextSwitch(&tm->oldTask->esp0, tm->current->esp0, toCR3(tm->current->taskMemory->linear.page));
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

void schedule(){
	taskSwitch(NULL, 0);
}

static int initV8086Memory(void){
	PageManager *p = processorLocalTask()->taskMemory->linear.page;
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
	uint32_t esp0, uint32_t espInterrupt, void *stackBottom, TaskMemoryManager *taskMemory, int priority
){
	Task *NEW(t);
	EXPECT(t != NULL);
	t->esp0 = esp0;
	t->espInterrupt = espInterrupt;
	t->stackBottom = stackBottom;

	t->ioSemaphore = createSemaphore();
	EXPECT(t->ioSemaphore != NULL);
	t->ioListLock = initialSpinlock;
	t->pendingIOList = NULL;
	t->finishedIOList = NULL;
	t->taskMemory = taskMemory;
	addReference(taskMemory, 1);
	t->state = SUSPENDED;
	t->priority = priority;
	t->taskDefinedSystemCall = undefinedSystemCall;
	t->taskDefinedArgument = 0;
	t->next =
	t->prev = NULL;

	return t;
	//deleteSemaphore(t->ioSemaphore);
	ON_ERROR;
	DELETE(t);
	ON_ERROR;
	return NULL;
}

#define KERNEL_STACK_SIZE ((size_t)8192)
#define MIN_BLOCK_MANAGER_PAGE_SIZE ((size_t)PAGE_SIZE)
static_assert(KERNEL_STACK_SIZE % PAGE_SIZE == 0);

static Task *createKernelTask(void (*eip0)(void), int priority, TaskMemoryManager *tm){
	// kernel task stack
	void *stackBottom = allocateKernelPages(KERNEL_STACK_SIZE, KERNEL_PAGE);
	EXPECT(stackBottom != NULL);
	uintptr_t stackTop = ((uintptr_t)stackBottom) + KERNEL_STACK_SIZE;
	// create task
	EFlags eflags = getEFlags();
	eflags.bit.interrupt = 0;
	uintptr_t initialESP0 = stackTop -
		initTaskStack(eflags.value, (uint32_t)eip0, stackTop);
	Task *t = createTask(initialESP0, stackTop - 4, stackBottom, tm, priority);
	EXPECT(t != NULL);
	return t;
	//DELETE(t);
	ON_ERROR;
	checkAndReleaseKernelPages(stackBottom);
	ON_ERROR;
	return NULL;
}

// TODO: user space stack, entry point
Task *createUserTask(void (*eip0)(void), int priority){
	const uintptr_t targetBlockManager = FLOOR(USER_LINEAR_END - maxLinearBlockManagerSize, PAGE_SIZE);
	const uintptr_t targetPageTable = targetBlockManager - sizeOfPageTableSet;
	const uintptr_t heapEnd = targetPageTable;
	assert(minLinearBlockManagerSize <= MIN_BLOCK_MANAGER_PAGE_SIZE);
	// 1. task PageManager
	// IMPROVE: map to user space
	PageManager *pageManager = createAndMapUserPageTable(
		targetPageTable, targetBlockManager + MIN_BLOCK_MANAGER_PAGE_SIZE, targetPageTable);
	EXPECT(pageManager != NULL);
	// 2. task MemoryBlock
	int ok = mapPage_L(pageManager, (void*)targetBlockManager, MIN_BLOCK_MANAGER_PAGE_SIZE, KERNEL_PAGE);
	EXPECT(ok);
	LinearMemoryBlockManager *mappedBlockManager =
		mapExistingPagesToKernel(pageManager, targetBlockManager, MIN_BLOCK_MANAGER_PAGE_SIZE, KERNEL_PAGE);
	EXPECT(mappedBlockManager != NULL);
	LinearMemoryBlockManager *_mappedBlockManager = createLinearBlockManager(
		(uintptr_t)mappedBlockManager, maxLinearBlockManagerSize, MIN_BLOCK_SIZE, MIN_BLOCK_SIZE, heapEnd);
	assert(_mappedBlockManager == mappedBlockManager);
	TaskMemoryManager *tm = createTaskMemory(pageManager, (LinearMemoryBlockManager*)targetBlockManager);
	EXPECT(tm != NULL);
	Task *t = createKernelTask(eip0, priority, tm);
	EXPECT(t != NULL);
	unmapKernelPages(_mappedBlockManager);
	unmapUserPageTableSet(pageManager);
	// 5. see startTask
	return t;
	// DELETE(t)
	ON_ERROR;
	assert(tm->referenceCount == 0);
	deleteTaskMemory(tm);
	ON_ERROR;
	unmapKernelPages(mappedBlockManager);
	ON_ERROR;
	unmapPage_L(pageManager, (void*)targetBlockManager, PAGE_SIZE);
	ON_ERROR;
	releasePageTable(pageManager);
	ON_ERROR;
	return NULL;
}

static void createThreadHandler(InterruptParam *p){
	sti();
	void (*entry)(void) = (void (*)(void))SYSTEM_CALL_ARGUMENT_0(p);
	Task *current = processorLocalTask();
	Task *newTask = createKernelTask(entry, current->priority, current->taskMemory);
	if(newTask != NULL){
		resume(newTask);
	}
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)newTask;
}

uintptr_t systemCall_createThread(void(*entry)(void)){
	return systemCall2(SYSCALL_CREATE_THREAD, (uintptr_t)entry);
}

static int tryToCancelIO(Task *t, IORequest *ior);

static void cancelAllIORequests(void){
	Task *t = processorLocalTask();
	// cancel or wait all IORequest
	while(1){
		acquireLock(&t->ioListLock);
		IORequest *ior = (t->pendingIOList != NULL? t->pendingIOList: t->finishedIOList);
		releaseLock(&t->ioListLock);
		if(ior == NULL)
			break;
		if(tryToCancelIO(t, ior))
			continue;
		waitIO(t, ior);
		if(tryToCancelIO(t, ior))
			continue;
		panic("cannot cancel All IO requests");
	}
}

typedef struct TaskQueueAndLock{
	TaskQueue queue;
	Spinlock lock;
}TaskQueueAndLock;

static void clearTerminateQueue(TaskQueueAndLock *ql){
	while(1){
		acquireLock(&ql->lock);
		Task *t = popQueue(&ql->queue);
		releaseLock(&ql->lock);
		if(t == NULL)
			break;
		assert(t->state == SUSPENDED);
		//printk("clearTerminateQueue: %x\n",t);
		if(checkAndReleaseKernelPages(t->stackBottom) == 0){
			panic("");
		}
		DELETE(t);
	}
}

static void pushTerminateQueue(Task *oldTask, uintptr_t arg){
	// cannot releaseKernelMemory(oldTask) here because interrupt is disabled
	// push oldTask into the queue and delete it later
	TaskQueueAndLock *ql = (TaskQueueAndLock*)arg;
	acquireLock(&ql->lock);
	pushQueue(&ql->queue, oldTask);
	//printk("pushTerminateQueue: %x\n",oldTask);
	releaseLock(&ql->lock);
}

void terminateCurrentTask(void){
	static TaskQueueAndLock terminateQueue = {INITIAL_TASK_QUEUE, INITIAL_SPINLOCK};
	clearTerminateQueue(&terminateQueue);
	cancelAllIORequests();
	Task *t = processorLocalTask();
	// delete ioSemaphore
	deleteSemaphore(t->ioSemaphore);
	t->ioSemaphore = NULL;
	// delete user space
	TaskMemoryManager *tmm = t->taskMemory;
	int refCnt = addReference(tmm, -1);
	if(refCnt == 0){
		PageManager *p = tmm->linear.page;
		// delete linear
		releaseAllLinearBlocks(&tmm->linear);
		unmapPage_L(p, tmm->linear.linear, MIN_BLOCK_MANAGER_PAGE_SIZE);
		// delete page
		cli();
		// temporary page manager; see deleteOldTask
		// addReference(kernelTaskMemory);
		t->taskMemory = kernelTaskMemory;
		invalidatePageTable(p, kernelTaskMemory->linear.page);
		sti();
		deleteTaskMemory(tmm);
		releaseInvalidatedPageTable(p);
	}
	else{
		cli();
		// addReference(kernelTaskMemory);
		t->taskMemory = kernelTaskMemory;
		sti();
	}
	cli();
	taskSwitch(pushTerminateQueue, (uintptr_t)&terminateQueue);
	assert(0); // never return
}

static void terminateHandler(__attribute__((__unused__)) InterruptParam *p){
	sti();
	terminateCurrentTask();
}

void systemCall_terminate(void){
	systemCall1(SYSCALL_TERMINATE);
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
	return &t->taskMemory->linear;
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
	assert(kernelTaskMemory != NULL);
	// each processor needs an idle task
	// create a task for current running bootstrap thread. not need to initialize eip and esp
	TaskManager *NEW(tm);
	if(tm == NULL){
		panic("cannot initialize task manager");
	}
	tm->current = createTask(/*esp0*/0, /*espInterrupt*/0, /*stackBottom*/0, kernelTaskMemory, NUMBER_OF_PRIORITIES - 1);
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

void resumeTaskByIO(IORequest *ior){
	Task *t = ior->task;
	acquireLock(&t->ioListLock);
	REMOVE_FROM_DQUEUE(ior); // t->pendingIOList
	ADD_TO_DQUEUE(ior, &(t->finishedIOList));
	ior->cancellable = 1;
	releaseLock(&t->ioListLock);
	releaseSemaphore(t->ioSemaphore);
}

static int searchIOList(Task *t, IORequest *ior){
	int found = 0;
	acquireLock(&t->ioListLock);
	IORequest *i;
	for(i = t->pendingIOList; found == 0 && i != NULL; i = i->next){
		found += (i == ior);
	}
	for(i = t->finishedIOList; found == 0 && i != NULL; i = i->next){
		found += (i == ior);
	}
	releaseLock(&t->ioListLock);
	return found;
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
	Task *t = processorLocalTask();
	if(searchIOList(t, ior) == 0){
		SYSTEM_CALL_RETURN_VALUE_0(p) = IO_REQUEST_FAILURE;
		return;
	}
	ior = waitIO(t, ior);
	uintptr_t rv[SYSTEM_CALL_MAX_RETURN_COUNT];
	rv[0] = (uintptr_t)ior;
	int returnCount = ior->finish(ior, rv + 1);
	assert(returnCount + 1 <= SYSTEM_CALL_MAX_RETURN_COUNT);
	copyReturnValues(p, rv, returnCount + 1);
}

uintptr_t systemCall_waitIO(uintptr_t ioNumber){
	return systemCall2(SYSCALL_WAIT_IO, ioNumber);
}

uintptr_t systemCall_waitIOReturn(uintptr_t ioNumber, int returnCount, ...){
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
	(*returnValues[0]) = ioNumber;
	uintptr_t rv0 = systemCall6Return(
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

static int tryToCancelIO(Task *t, IORequest *ior){
	acquireLock(&t->ioListLock);
	int ok = ior->cancellable;
	if(ok){
		REMOVE_FROM_DQUEUE(ior);
	}
	releaseLock(&t->ioListLock);
	if(ok){
		ior->cancel(ior);
	}
	return ok;
}

static void cacnelIOHandler(InterruptParam *p){
	sti();
	IORequest *ior = (IORequest*)SYSTEM_CALL_ARGUMENT_0(p);
	Task *t = processorLocalTask();
	SYSTEM_CALL_RETURN_VALUE_0(p) = (
		searchIOList(t, ior)?
		(unsigned)tryToCancelIO(t, ior):
		0
	);
}

int systemCall_cancelIO(uintptr_t io){
	return (int)systemCall2(SYSCALL_CANCEL_IO, io);
}

void setCancellable(IORequest *ior, int value){
	acquireLock(&ior->task->ioListLock);
	ior->cancellable = value;
	releaseLock(&ior->task->ioListLock);
}

void initIORequest(
	IORequest *this,
	void *instance,
	/*HandleIORequest h,
	Task *t,*/
	CancelIORequest *cancelIORequest,
	FinishIORequest *finishIORequest
){
	this->ioRequest = instance;
	this->prev = NULL;
	this->next = NULL;
	this->handle = resumeTaskByIO;
	this->task = processorLocalTask();
	this->cancel = cancelIORequest;
	this->cancellable = 1;
	this->finish = finishIORequest;
}

static void allocateHeapHandler(InterruptParam *p){
	sti();
	uintptr_t size = SYSTEM_CALL_ARGUMENT_0(p);
	PageAttribute attribute = SYSTEM_CALL_ARGUMENT_1(p);
	size = CEIL(size, PAGE_SIZE);
	void *ret = allocatePages(&processorLocalTask()->taskMemory->linear, size, attribute);
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)ret;
}

void *systemCall_allocateHeap(uintptr_t size, PageAttribute attribute){
	uintptr_t address = systemCall3(SYSCALL_ALLOCATE_HEAP, size, attribute);
//assert(address != UINTPTR_NULL);
	return (void*)address;
}

static void releaseHeapHandler(InterruptParam *p){
	sti();
	uintptr_t address = SYSTEM_CALL_ARGUMENT_0(p);
	int ret = checkAndReleasePages(&processorLocalTask()->taskMemory->linear, (void*)address);
	SYSTEM_CALL_RETURN_VALUE_0(p) = ret;
}

int systemCall_releaseHeap(void *address){
	uintptr_t ok = systemCall2(SYSCALL_RELEASE_HEAP, (uintptr_t)address);
	assert(ok); // TODO: remove this when we start working on user space tasks
	return (int)ok;
}

static void translatePageHandler(InterruptParam *p){
	uintptr_t address = SYSTEM_CALL_ARGUMENT_0(p);
	PhysicalAddress ret = checkAndTranslatePage(
		&processorLocalTask()->taskMemory->linear, (void*)address);
	SYSTEM_CALL_RETURN_VALUE_0(p) = ret.value;
}

PhysicalAddress systemCall_translatePage(void *address){
	PhysicalAddress p = {systemCall2(SYSCALL_TRANSLATE_PAGE, (uintptr_t)address)};
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
	kernelTaskMemory = createTaskMemory(kernelLinear->page, kernelLinear->linear/*NULL*/);
	if(kernelTaskMemory == NULL){
		panic("cannot create kernel task memory");
	}
	registerSystemCall(systemCallTable, SYSCALL_TASK_DEFINED, taskDefinedHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_WAIT_IO, waitIOHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_CANCEL_IO, cacnelIOHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_ALLOCATE_HEAP, allocateHeapHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_RELEASE_HEAP, releaseHeapHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_TRANSLATE_PAGE, translatePageHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_CREATE_THREAD, createThreadHandler, 0);
	registerSystemCall(systemCallTable, SYSCALL_TERMINATE, terminateHandler, 0);
	//initSemaphore(systemCallTable);
}

#ifndef NDEBUG
int getFreeBlockSize(MemoryBlockManager *m);
void releaseAllLinearBlocks(LinearMemoryManager *m);
void testMemoryTask(void){
	int a,loop;
	int r = 47;
	//sleep(10);
	for(loop=0;loop<10;loop++){//hlt();continue;

		LinearMemoryManager *lm = &processorLocalTask()->taskMemory->linear;
		// size_t phyFr[4], linFr[4];
		// phyFr[0] = getFreeBlockSize(kernelLinear->physical);
		// linFr[0] = getFreeBlockSize(lm->linear);
		releaseAllLinearBlocks(lm);
		// phyFr[1] = getFreeBlockSize(kernelLinear->physical);
		// linFr[1] = getFreeBlockSize(lm->linear);

		for(a=0;a<5;a++){
			unsigned s = (r&0xffff)*PAGE_SIZE;
			r=(r*7+5)%101+1;
			uint8_t *buf = systemCall_allocateHeap(s, KERNEL_PAGE);
			if(buf==NULL)
				continue;
			unsigned b;
			for(b=0;b<30&&b<s;b++){
				buf[b] = buf[s-1-b]= 'Z';
			}
		}
		/*
		phyFr[2] = getFreeBlockSize(kernelLinear->physical);
		linFr[2] = getFreeBlockSize(lm->linear);
		releaseAllLinearBlocks(lm);
		phyFr[3] = getFreeBlockSize(kernelLinear->physical);
		linFr[3] = getFreeBlockSize(lm->linear);
		printk("physical Free: %x %x %x %x\n", phyFr[0], phyFr[1], phyFr[2], phyFr[3]);
		printk("linear Free  : %x %x %x %x\n", linFr[0], linFr[1], linFr[2], linFr[3]);
		*/
	}
	systemCall_terminate();
}

static void threadEntry(void){
	printk("thread_created\n");
	systemCall_terminate();
}

void testCreateThread(void){
	static int failed=0;
	int a;
	for(a = 0; failed == 0; a++){
		if(systemCall_createThread(testCreateThread) == UINTPTR_NULL){
			failed=1;
			printk("failed\n");
		}
	}
	threadEntry();
}

#endif
