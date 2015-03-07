#include"task.h"
#include"segment/segment.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"memory/page.h"
#include"interrupt/handler.h"
#include"multiprocessor/spinlock.h"
#include"common.h"

typedef struct TaskStateSegment{
	uint16_t previousTaskLink, reserved0;
	uint32_t esp0;
	uint16_t ss0, reserved1;
	uint32_t esp1;
	uint16_t ss1, reserved2;
	uint32_t esp2;
	uint16_t ss2, reserved3;
	uint32_t cr3, eip, eflags,
	eax, ecx, edx, ebx, esp, ebp, esi, edi;
	uint16_t
	es, reserved4,
	cs, reserved5,
	ss, reserved6,
	ds, reserved7,
	fs, reserved8,
	gs, reserved9,
	ldtSelector, reserved10,
	degubTrap: 1,
	reserved11: 15,
	ioBitmapAddress;
}TSS;

static_assert(sizeof(TSS) == 104);

typedef struct Task{
	// context
	struct InterruptParam interruptParam;
	// SegmentTable *ldt;
	PageDirectory *pageTable;

	// data
	enum TaskState{
		RUNNING,
		READY,
		SUSPEND,
	}state;
	int priority;
	struct Task *next, *prev;
}Task;

static void startTask(void){
	while(1){
		kprintf("startTask");
		hlt();
	}
}

static struct Task *createTask(BlockManager *p){
	uintptr_t esp0 = (uintptr_t)allocateBlock(p, MIN_BLOCK_SIZE);
	esp0 += MIN_BLOCK_SIZE;
	esp0 -= sizeof(Task);
	Task *t = (Task*)esp0;
	// Task *NEW(t, m);
	t->state = READY;
	MEMSET0(&t->interruptParam);
	t->interruptParam.cs = getCS();
	t->interruptParam.ds =
	t->interruptParam.es =
	t->interruptParam.fs =
	t->interruptParam.gs =
	t->interruptParam.ss = getDS();
	t->interruptParam.eip = (uint32_t)startTask;
	t->interruptParam.esp =
	t->interruptParam.ebp = esp0 - 4;
	t->interruptParam.eflags = getEFlags();
	// TODO: ldt
	// TODO pageTable
	return t;
}

#define NUMBER_OF_PRIORITY (4)
struct TaskManager{
	Task *current;
	SegmentSelector *tssSelector;
};
static struct TaskQueue{
	Task *next;
}*globalQueue = NULL;
static Spinlock *globalQueueLock = NULL;

static void contextSwitch(Task *oldTask, Task *newTask, InterruptParam *currentContext){
	memcpy(&oldTask->interruptParam, currentContext, sizeof(InterruptParam));
	memcpy(currentContext, &newTask->interruptParam, sizeof(InterruptParam));
	// oldTask->ldt
	// oldTask->pageTable = (PageDirectory*)getCR3();
	// setCR3((uint32_t)newTask->pageTable);
}

void schedule(TaskManager *tm, InterruptParam *p){p->eip = (uint32_t)startTask;return;
	struct TaskQueue *q;
	struct Task *oldTask = tm->current;
	acquireLock(globalQueueLock);
	if(tm->current->state == RUNNING){
		q = &globalQueue[oldTask->priority];
		oldTask->state = READY;
		if(q->next == NULL){
			q->next =
			oldTask->next =
			oldTask->prev = oldTask;
		}
		else{
			oldTask->next = q->next;
			oldTask->prev = q->next->prev;
			oldTask->next->prev = oldTask;
			oldTask->prev->next = oldTask;
		}
	}
	for(q = globalQueue; q->next == NULL; q++);
	struct Task *newTask = tm->current = q->next;
	if(newTask->next == newTask/* && newTask->prev == newTask*/){
		q->next = NULL;
	}
	else{
		q->next = newTask->next;
		newTask->next->prev = newTask->prev;
		newTask->prev->next = newTask->next;
	}
	releaseLock(globalQueueLock);
	newTask->next = newTask->prev = NULL;
	if(oldTask != newTask){
		contextSwitch(oldTask, newTask, p);
	}
}

void loadTaskRegister(TaskManager *tm){
	uint16_t s = toShort(tm->tssSelector);
	__asm__(
	"ltr %0\n"
	:
	:"a"(s)
	);
}

TaskManager *createTaskManager(
	MemoryManager *m,
	BlockManager *b,
	SegmentTable *gdt,
	SegmentSelector *kernelSS,
	uint32_t kernelESP
){
	TaskManager *NEW(tm, m);
	tm->current = createTask(b); // TODO:
	if(globalQueue == NULL){
		NEW_ARRAY(globalQueue, m, NUMBER_OF_PRIORITY);
		globalQueueLock = createSpinlock(m);
		int t;
		for(t = 0; t < NUMBER_OF_PRIORITY; t++){
			globalQueue[t].next = NULL;
		}
	}
	TSS *NEW(tss, m);
	memset(tss, 0, sizeof(TSS));
	tss->ss0 = toShort(kernelSS);
	tss->esp0 = kernelESP;
	tss->ioBitmapAddress = 0x0fff;//sizeof(TSS);
	tm->tssSelector = addSegment(gdt, (uintptr_t)tss, sizeof(TSS) - 1, KERNEL_TSS);
	return tm;
}
