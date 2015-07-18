#include<common.h>
#include"task_private.h"
#include"memory/memory.h"
#include"memory/memory_private.h"



int initTaskMemoryBlock(TaskMemoryManager *m, size_t blockManagerSize){
	int ok = mapPage_L(m->pageManager, m->blockManager, PAGE_SIZE, KERNEL_PAGE);
	EXPECT(ok);
	MemoryBlockManager *ok2 = createMemoryBlockManager((uintptr_t)m->blockManager,
		blockManagerSize, m->heapBegin, m->heapBegin, m->heapEnd);
	EXPECT(ok2 == m->blockManager);
	return 1;
	ON_ERROR;
	unmapPage_L(m->pageManager, m->blockManager, PAGE_SIZE);
	ON_ERROR;
	return 0;
}

int initTaskMemory(TaskMemoryManager *m, PageManager *p, MemoryBlockManager *b, uintptr_t heapBegin, uintptr_t heapEnd){
	m->pageManager = p;
	m->blockManager = b;
	m->heapBegin = heapBegin;
	m->heapEnd = heapEnd;
	return 1;
}
