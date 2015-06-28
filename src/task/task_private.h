#include<std.h>
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"

// taskmanager.c
typedef struct TaskQueue{
	struct Task *volatile head;
}TaskQueue;

#define INITIAL_TASK_QUEUE {NULL}
extern const TaskQueue initialTaskQueue;

// put task to the last position of the queue
void pushQueue(TaskQueue *q, struct Task *t);
// get the first task in the queue
struct Task *popQueue(TaskQueue *q);

void suspendCurrent(void (*afterTaskSwitchFunc)(struct Task*, uintptr_t), uintptr_t arg);

// semaphore.c
typedef struct SystemCallTable SystemCallTable;
void initSemaphore(SystemCallTable *systemCallTable);

// taskmemory.c
typedef struct PageManager PageManager;
typedef struct{
	uintptr_t userStackTop, userStackBottom;
	struct HeapManager *heapAllocation;
	uintptr_t userHeapBottom;
	PageManager *pageManager;
}TaskMemoryManager;

// allocate physical memory for 0 ~ userHeapBottom
int initTaskMemory(TaskMemoryManager *m, PageManager *p, uintptr_t userStackTop, uintptr_t userHeapBottom);
int extendHeap(TaskMemoryManager *m, size_t size, PageAttribute attribute);
int shrinkHeap(TaskMemoryManager *m, size_t size);
