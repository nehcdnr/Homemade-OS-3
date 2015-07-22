#include<common.h>
#include"task_private.h"
#include"memory/memory.h"
#include"memory/memory_private.h"
#include"interrupt/systemcall.h"
#include"multiprocessor/processorlocal.h"

struct TaskMemoryManager{
	LinearMemoryManager linear;
	uintptr_t heapBegin;
	uintptr_t heapEnd;
};

PageManager *getPageManager(TaskMemoryManager *m){
	return m->linear.page;
}

LinearMemoryManager *getLinearMemoryManager(TaskMemoryManager *m){
	return &m->linear;
}

TaskMemoryManager *createTaskMemory(PageManager *p, MemoryBlockManager *b, uintptr_t heapBegin, uintptr_t heapEnd){
	TaskMemoryManager *NEW(m);
	if(m == NULL)
		return NULL;
	assert(p != NULL);
	m->linear.page = p;
	m->linear.linear = b;
	m->linear.physical = kernelLinear->physical;
	m->heapBegin = heapBegin;
	m->heapEnd = heapEnd;
	return m;
}
