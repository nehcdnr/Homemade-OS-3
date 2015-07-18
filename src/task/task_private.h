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
typedef struct{
	//LinearMemoryManager linear;
	uintptr_t heapBegin;
	uintptr_t heapEnd;
	struct PageManager *pageManager;
	struct MemoryBlockManager *blockManager;
}TaskMemoryManager;

int initTaskMemory(TaskMemoryManager *m, struct PageManager *p, struct MemoryBlockManager *b,
	uintptr_t heapBegin, uintptr_t heapEnd);

int initTaskMemoryBlock(TaskMemoryManager *m, size_t blockManagerSize);
