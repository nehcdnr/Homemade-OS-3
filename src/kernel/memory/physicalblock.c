#include"buddy.h"

typedef struct PhysicalMemoryBlock{
	uint32_t referenceCount;
	MemoryBlock block;
}PhysicalMemoryBlock;

#define MAX_REFERENCE_COUNT ((uint32_t)0xffffffff)

struct PhysicalMemoryBlockManager{
	MemoryBlockManager b;
};

static_assert(MEMBER_OFFSET(PhysicalMemoryBlockManager, b.blockArray) == sizeof(PhysicalMemoryBlockManager));

static void initPhysicalMemoryBlock(void *b){
	PhysicalMemoryBlock *pb = b;
	initMemoryBlock(&pb->block);
	pb->referenceCount = 1;
}

PhysicalMemoryBlockManager *createPhysicalMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr
){
	PhysicalMemoryBlockManager *pm = (PhysicalMemoryBlockManager*)manageBase;
	initMemoryBlockManager(
		&pm->b,
		sizeof(PhysicalMemoryBlock), MEMBER_OFFSET(PhysicalMemoryBlock, block),
		beginAddr, initEndAddr,
		initPhysicalMemoryBlock
	);
	if(getPhysicalBlockManagerSize(pm) >= manageSize){
		panic("cannot initialize physical memory manager");
	}
	return pm;
}

size_t getPhysicalBlockManagerSize(PhysicalMemoryBlockManager *m){
	return sizeof(*m) + m->b.blockCount * m->b.blockStructSize;
}

int getPhysicalBlockCount(PhysicalMemoryBlockManager *m){
	return m->b.blockCount;
}

size_t getFreePhysicalBlockSize(PhysicalMemoryBlockManager *m){
	return m->b.freeSize;
}

uintptr_t getPhysicalBeginAddress(PhysicalMemoryBlockManager *m){
	return m->b.beginAddress;
}

uintptr_t allocatePhysicalBlock(PhysicalMemoryBlockManager *m, size_t *size){
	MemoryBlock *b;
	PhysicalMemoryBlock *pmb;
	acquireLock(&m->b.lock);
	b = allocateBlock_noLock(&m->b, size, 0);
	if(b != NULL){
		pmb = blockToElement(&m->b, b);
		assert(pmb->referenceCount == 0);
		pmb->referenceCount = 1;
	}
	releaseLock(&m->b.lock);
	return (b== NULL? UINTPTR_NULL: elementToAddress(&m->b, pmb));
}

int addPhysicalBlockReference(PhysicalMemoryBlockManager *m, uintptr_t address){
	int ok;
	acquireLock(&m->b.lock);
	PhysicalMemoryBlock *pmb = addressToElement(&m->b, address);
	assert(pmb->referenceCount > 0);
	ok = (pmb->referenceCount < MAX_REFERENCE_COUNT);
	if(ok){
		pmb->referenceCount++;
	}
	releaseLock(&m->b.lock);
	return ok;
}

void releaseOrUnmapPhysicalBlock(PhysicalMemoryBlockManager *m, uintptr_t address){
	acquireLock(&m->b.lock);
	PhysicalMemoryBlock *pmb = addressToElement(&m->b, address);
	assert(pmb->referenceCount > 0);
	pmb->referenceCount--;
	if(pmb->referenceCount == 0){
		releaseBlock_noLock(&m->b, &pmb->block);
	}
	releaseLock(&m->b.lock);
}
