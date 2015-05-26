#include<std.h>
#include"memory/memory.h"
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

#define getPageManager(TASK_MEMORY) ((TASK_MEMORY)->pageManager)

// allocate physical memory for 0 ~ userHeapBottom
int initTaskMemory(TaskMemoryManager *m, PageManager *p, uintptr_t userStackTop, uintptr_t userHeapBottom);
int extendHeap(TaskMemoryManager *m, size_t size, PageAttribute attribute);
int shrinkHeap(TaskMemoryManager *m, size_t size);
