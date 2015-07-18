#include<common.h>
#include"task_private.h"
#include"memory/memory.h"
#include"memory/memory_private.h"

int initTaskMemory(TaskMemoryManager *m, PageManager *p, MemoryBlockManager *b, uintptr_t heapBegin, uintptr_t heapEnd){
	m->pageManager = p;
	m->blockManager = b;
	m->heapBegin = heapBegin;
	m->heapEnd = heapEnd;
	return 1;
}
