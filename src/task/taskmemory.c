#include<common.h>
#include"task_private.h"
#include"memory/memory.h"

typedef struct HeapManager{
	struct HeapManager *next;
	int count;
	struct HeapAllocation{
		uintptr_t beginAddress;
		uintptr_t endAddress;
		PageAttribute attribute;
	}allocation[0];
}HeapManager;

#define HEAP_MANAGER_SIZE (PAGE_SIZE)
// NULL is a valid address for HeapManager because heap section may start from 0
#define INVALID_HEAP_ADDRESS ((HeapManager*)0xffffffff)
#undef NULL

#define HEAP_ALLOCATION_LENGTH ((HEAP_MANAGER_SIZE - sizeof(struct HeapManager))/sizeof(struct HeapAllocation))

static HeapManager *initHeapAllocationList(PageManager *p, uintptr_t heapBottom, HeapManager *nextHeap){
	HeapManager *ha = (HeapManager*)heapBottom;
	if(mapPage_L(p, ha, HEAP_MANAGER_SIZE, KERNEL_PAGE) == 0){
		return INVALID_HEAP_ADDRESS;
	}
	ha->count = 0;
	ha->next = nextHeap;
	return ha;
}
static uintptr_t heapAllocationEnd(HeapManager *ha){
	if(ha->count > 0)
		return ha->allocation[ha->count - 1].endAddress;
	else
		return ((uintptr_t)ha) + HEAP_MANAGER_SIZE;
}

static HeapManager *addHeapAllocation(
	PageManager *p,
	HeapManager *ha,
	size_t size, PageAttribute attribute
){
	struct HeapAllocation *newAllocation;
	if(ha != INVALID_HEAP_ADDRESS && ha->count > 0 && ha->allocation[ha->count - 1].attribute == attribute){
		newAllocation = &(ha->allocation[ha->count - 1]);
	}
	if(ha->count == HEAP_ALLOCATION_LENGTH){
		HeapManager *newHeap = initHeapAllocationList(p, ha->allocation[ha->count-1].endAddress, ha);
		if(newHeap == INVALID_HEAP_ADDRESS){
			return INVALID_HEAP_ADDRESS;
		}
		newAllocation = newHeap->allocation + 0;
		newAllocation->beginAddress =
		newAllocation->endAddress = heapAllocationEnd(newHeap);
		newAllocation->attribute = attribute;
		newHeap->count++;
	}
	int mapPageOK = mapPage_L(p, (void*)(newAllocation->endAddress), size, attribute);
	// on failure, leave a new heapManger with count == 0
	EXPECT(mapPageOK != 0);
	newAllocation->endAddress += size;
	return ha;

	ON_ERROR;
	return INVALID_HEAP_ADDRESS;
}

static HeapManager *removeHeapAllocation(PageManager *p, HeapManager *ha, uintptr_t size){
	while(size != 0){
		assert(ha != INVALID_HEAP_ADDRESS);
		while(size != 0 && ha->count != 0){
			struct HeapAllocation *al = ha->allocation + ha->count - 1;
			size_t releaseSize = (size < al->endAddress - al->beginAddress? size: al->endAddress - al->beginAddress);
			al->endAddress -= releaseSize;
			// if(al->attribute == ??)
			unmapPage_L(p, (void*)(al->endAddress), releaseSize);
			size -= releaseSize;
			assert((size == 0) || (al->endAddress == al->beginAddress));
			if(al->endAddress == al->beginAddress){
				ha->count--;
			}
		}
		if(ha->count == 0){
			HeapManager *ha2 = ha;
			ha = ha->next;
			unmapPage_L(p, ha2, HEAP_MANAGER_SIZE);
		}
	}
	return ha;
}

// TaskMemoryManager

int initTaskMemory(TaskMemoryManager *m, PageManager *p, uintptr_t userStackTop, uintptr_t userHeapBottom){
	m->userStackBottom =
	m->userStackTop = userStackTop;
	m->userHeapBottom = userHeapBottom;
	m->pageManager = p;

	assert(userStackTop % PAGE_SIZE == 0 && userHeapBottom % PAGE_SIZE == 0);
	// do not allocate heapManager in initialization
	// int mapPageOK;
	// mapPageOK = mapPage_L(p, (void*)0, userHeapBottom, USER_WRITABLE_PAGE);
	//EXPECT(mapPageOK != 0);
	m->heapAllocation = INVALID_HEAP_ADDRESS;

	return 1;

	//unmapPage_L(p, (void*)0, userHeapBottom);
	//ON_ERROR;
	// return 0;
}

#define MAX_ENTEND_SIZE ((size_t)0x10000000)

static uintptr_t heapTop(TaskMemoryManager *m){
	return m->heapAllocation == INVALID_HEAP_ADDRESS? m->userHeapBottom: heapAllocationEnd(m->heapAllocation);
}

int extendHeap(TaskMemoryManager *m, size_t size, PageAttribute attribute){
	if(size > MAX_ENTEND_SIZE || size % PAGE_SIZE != 0 ||
	m->userStackBottom < size || m->userStackBottom < heapTop(m) + size){
		return 0;
	}
	HeapManager *newHeap;
	if(m->heapAllocation == INVALID_HEAP_ADDRESS){
		newHeap = initHeapAllocationList(m->pageManager, m->userHeapBottom, INVALID_HEAP_ADDRESS);
	}
	else{
		newHeap = addHeapAllocation(m->pageManager, m->heapAllocation, size, attribute);
	}
	if(newHeap == INVALID_HEAP_ADDRESS)
		return 0;
	m->heapAllocation = newHeap;
	return 1;
}

int shrinkHeap(TaskMemoryManager *m, size_t size){
	if(size % PAGE_SIZE != 0 || size > heapTop(m) - m->userHeapBottom){
		return 0;
	}
	m->heapAllocation = removeHeapAllocation(m->pageManager, m->heapAllocation, size);
	return 1;
}
