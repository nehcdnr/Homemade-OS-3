#include"memory_private.h"
#include"buddy.h"

enum MemoryBlockStatus{
	MEMORY_FREE_OR_COVERED = 1, // maybe free
	MEMORY_USING = 2, // using
	MEMORY_LOCKED = 3, // releasing or allocating
}__attribute__((__packed__));

typedef struct LinearMemoryBlock{
	size_t mappedSize;
	enum MemoryBlockStatus status;
	MemoryBlock block;
}LinearMemoryBlock;

static_assert(sizeof(LinearMemoryBlock) == 20);

struct LinearMemoryBlockManager{
	int initialBlockCount, maxBlockCount;
	MemoryBlockManager b;
};
static_assert(MEMBER_OFFSET(LinearMemoryBlockManager, b.blockArray) == sizeof(LinearMemoryBlockManager));

static void initLinearMemoryBlock(void *voidLMB){
	LinearMemoryBlock *lmb = voidLMB;
	lmb->mappedSize = MIN_BLOCK_SIZE;
	lmb->status = MEMORY_USING;
	initMemoryBlock(&lmb->block);
}

uintptr_t getInitialLinearBlockEnd(LinearMemoryBlockManager *bm){
	return (uintptr_t)indexToElement(&bm->b, bm->initialBlockCount);
}

uintptr_t evaluateLinearBlockEnd(uintptr_t manageBase, uintptr_t beginAddr, uintptr_t initEndAddr){
	return evaluateMemoryBlockManagerEnd(
		&(((LinearMemoryBlockManager*)manageBase)->b), sizeof(LinearMemoryBlock),
		beginAddr, initEndAddr
	);
}

LinearMemoryBlockManager *createLinearBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr,
	uintptr_t maxEndAddr
){
	LinearMemoryBlockManager *bm = (LinearMemoryBlockManager*)manageBase;
	initMemoryBlockManager(
		&bm->b, sizeof(LinearMemoryBlock), MEMBER_OFFSET(LinearMemoryBlock, block),
		beginAddr, initEndAddr,
		initLinearMemoryBlock
	);
	bm->initialBlockCount = bm->b.blockCount;
	bm->maxBlockCount = (maxEndAddr - beginAddr) / MIN_BLOCK_SIZE;

	if(getMaxLinearBlockManagerSize(bm) > manageSize){
		panic("buddy memory manager (linear) initialization error");
	}
	return bm;
}

size_t getAllocatedBlockSize(LinearMemoryBlockManager *m, uintptr_t address){
	LinearMemoryBlock *lmb = addressToElement(&m->b, address);
	// allow mappedSize == 0
	assert(lmb->mappedSize % PAGE_SIZE == 0);
	return lmb->mappedSize;
}

const size_t minLinearBlockManagerSize = sizeof(LinearMemoryBlockManager);
const size_t maxLinearBlockManagerSize = sizeof(LinearMemoryBlockManager) +
	((0xffffffff / MIN_BLOCK_SIZE) + 1) * sizeof(LinearMemoryBlock);

size_t getMaxLinearBlockManagerSize(LinearMemoryBlockManager *m){
	return sizeof(*m) + m->maxBlockCount * m->b.blockStructSize;
}

int getMaxBlockCount(LinearMemoryBlockManager *m){
	return m->maxBlockCount;
}

size_t getFreeLinearBlockSize(LinearMemoryBlockManager *m){
	return m->b.freeSize;
}

static int getExtendBlockCount(LinearMemoryBlockManager *m, size_t size){
	assert(isAcquirable(&m->b.lock) == 0);
	size_t i = ceilAllocateOrder(size);
	// too large
	if(i > MAX_BLOCK_ORDER){
		return 0;
	}
	int addBlockCount = (1 << (i - MIN_BLOCK_ORDER));
	int newBlockCount = addBlockCount + CEIL(m->b.blockCount, addBlockCount);
	if(newBlockCount > m->maxBlockCount){
		return 0;
	}
	return newBlockCount - m->b.blockCount;
}

// if MEMORY_RELEASING or MEMORY_FREE, return 0
// if MEMORY_USING, return 1
static int isUsingBlock_noLock(LinearMemoryBlockManager *m, uintptr_t address){
	MemoryBlock *b1, *b2;
	b1 = addressToBlock(&m->b, address);
	while(1){
		b2 = getBuddy(&m->b, b1);
		// b1 is not covered by other blocks
		if(b2 == NULL || b2->sizeOrder <= b1->sizeOrder){
			break;
		}
		assert(b2 < b1);
		b1 = b2;
	}
	return (((LinearMemoryBlock*)blockToElement(&m->b, b1))->status == MEMORY_USING);
}

// return whether a block is a valid argument of releaseBlock
static int isReleasableBlock_noLock(LinearMemoryBlockManager *m, LinearMemoryBlock *lmb){
#ifndef NDEBUG
	MemoryBlock *b1 = &lmb->block, *b2 = getBuddy(&m->b, b1);
#endif
	switch(lmb->status){
	case MEMORY_USING:
#ifndef NDEBUG
		assert(IS_IN_DQUEUE(b1) == 0);
		if(b2 != NULL){
			assert(b2->sizeOrder <= b1->sizeOrder);
		}
#endif
		return 1;
	case MEMORY_FREE_OR_COVERED:
#ifndef NDEBUG
		if(b2 != NULL){
			assert(IS_IN_DQUEUE(b1) || (b2 < b1 && b2->sizeOrder > b1->sizeOrder));
		}
		else{
			assert(IS_IN_DQUEUE(b1));
		}
#endif
	case MEMORY_LOCKED:
		return 0;
	default:
		assert(0);
		return 0;
	}
}

// LinearMemoryManager

static int extendLinearBlock_noLock(LinearMemoryManager *m, int exCount){
	LinearMemoryBlockManager *bm = m->linear;
	int newBlockCount = bm->b.blockCount + exCount;
	uintptr_t exPage = CEIL((uintptr_t)indexToElement(&bm->b, bm->b.blockCount), PAGE_SIZE);
	while(bm->b.blockCount < newBlockCount){
		LinearMemoryBlock *const lastBlock = indexToElement(&bm->b, bm->b.blockCount);
		if((uintptr_t)(lastBlock + 1) > exPage){
			// if mapPage_L fails, it calls _unmapPage which is not allowed in interrupt-disabled section
			// in order to prevent from calling _unmapPage, allocate only one page in each call
			int ok = _mapPage_L(m->page, m->physical, (void*)exPage, PAGE_SIZE, KERNEL_PAGE);
			if(!ok){
				break;
			}
			exPage += PAGE_SIZE;
		}
		bm->b.blockCount++;
		initLinearMemoryBlock(lastBlock);
		// updated bm->blockCount is accessed here
		lastBlock->status = MEMORY_FREE_OR_COVERED;
		releaseBlock_noLock(&bm->b, &lastBlock->block);
	}
	return bm->b.blockCount >= newBlockCount;
}

uintptr_t allocateLinearBlock(LinearMemoryManager *m, size_t size){
	LinearMemoryBlockManager *bm = m->linear;
	LinearMemoryBlock *lmb;
	MemoryBlock *block;
	acquireLock(&bm->b.lock);
	block = allocateBlock_noLock(&bm->b, size, size);
	if(block != NULL){ // ok
		goto allocate_return;
	}
	int exCount = getExtendBlockCount(bm, size);
	if(exCount == 0){ // error
		goto allocate_return;
	}
	if(extendLinearBlock_noLock(m, exCount) == 0){ // error
		// printk("warning: extendLinearBlock failed");
		goto allocate_return;
	}
	block = allocateBlock_noLock(&bm->b, size, size);
	//if(block != NULL){
	//	goto allocate_return;
	//}
	allocate_return:
	if(block != NULL){
		lmb = blockToElement(&bm->b, block);
		lmb->mappedSize = size;
		assert(lmb->status == MEMORY_FREE_OR_COVERED);
		lmb->status = MEMORY_LOCKED;
	}
	releaseLock(&bm->b.lock);
	if(block == NULL){
		return INVALID_PAGE_ADDRESS;
	}

	uintptr_t linearAddress = elementToAddress(&bm->b, lmb);
	return linearAddress;
}

void commitAllocatingLinearBlock(LinearMemoryManager *m, uintptr_t linearAddress){
	LinearMemoryBlockManager *bm = m->linear;
	acquireLock(&bm->b.lock);
	LinearMemoryBlock *lmb = addressToElement(&bm->b, linearAddress);
	assert(lmb->status == MEMORY_LOCKED);
	lmb->status = MEMORY_USING;
	releaseLock(&bm->b.lock);
}

void releaseLinearBlock(LinearMemoryBlockManager *m, uintptr_t address){
	acquireLock(&m->b.lock);
	LinearMemoryBlock *lmb = addressToElement(&m->b, address);
	assert(lmb->status == MEMORY_USING || lmb->status == MEMORY_LOCKED);
	lmb->status = MEMORY_FREE_OR_COVERED;
	releaseBlock_noLock(&m->b, &lmb->block);
	releaseLock(&m->b.lock);
}

int checkAndReleaseLinearBlock(LinearMemoryManager *m, uintptr_t linearAddress){
	LinearMemoryBlockManager *bm = m->linear;
	int r;
	acquireLock(&bm->b.lock);
	r = isAddressInRange(&bm->b, linearAddress);
	if(r == 0){
		goto release_return;
	}
	LinearMemoryBlock *lmb = addressToElement(&bm->b, linearAddress);
	r = isReleasableBlock_noLock(bm, lmb);
	if(r == 0){
		goto release_return;
	}
	size_t s = getAllocatedBlockSize(bm, linearAddress);
	assert(lmb->status == MEMORY_USING);
	lmb->status = MEMORY_LOCKED;
	releaseLock(&bm->b.lock);

	_unmapPage(m->page, m->physical, (void*)linearAddress, s);

	acquireLock(&bm->b.lock);
	assert(lmb->status == MEMORY_LOCKED);
	lmb->status = MEMORY_FREE_OR_COVERED;
	releaseBlock_noLock(&bm->b, &lmb->block);
	r = 1;
	release_return:
	releaseLock(&bm->b.lock);
	return r;
}

// release every m->linear->block and reset m->linear->blockCount to initialBlockCount
// not thread-safe
void releaseAllLinearBlocks(LinearMemoryManager *m){
	LinearMemoryBlockManager *bm = m->linear;
	int i = 0;
	while(i < bm->b.blockCount){
		LinearMemoryBlock *lmb =(LinearMemoryBlock*)indexToElement(&bm->b, i);
		assert(lmb->status != MEMORY_LOCKED);
		checkAndReleaseLinearBlock(m, blockToAddress(&bm->b, &lmb->block));
		// no lock
		// no matter the block is free, using, or covered, adding the block size does not skip any using block
		i += (1 << lmb->block.sizeOrder) / MIN_BLOCK_SIZE;
	}
	assert(i == bm->b.blockCount);
	// see allocateLinearBlock
	uintptr_t rlsPageBegin = CEIL(getInitialLinearBlockEnd(bm), PAGE_SIZE);
	uintptr_t rlsPageEnd = CEIL((uintptr_t)indexToElement(&bm->b, bm->b.blockCount), PAGE_SIZE);
	// see extendLinearBlock_noLock
	_unmapPage_L(m->page, m->physical, (void*)rlsPageBegin, rlsPageEnd - rlsPageBegin);
	resetBlockArray(&bm->b, bm->initialBlockCount, initLinearMemoryBlock);
}

static PhysicalAddress checkAndTranslateBlock(
	LinearMemoryManager *m, uintptr_t linearAddress,
	PageAttribute hasAttribute, int doReserve
){
	if(isKernelLinearAddress(linearAddress)){ // XXX:
		m = kernelLinear;
	}
	LinearMemoryBlockManager *bm = m->linear;
	PhysicalAddress p = {INVALID_PAGE_ADDRESS};
	acquireLock(&bm->b.lock);
	if(isAddressInRange(&bm->b, linearAddress) == 0)
		goto translate_return;
	if(isUsingBlock_noLock(bm, linearAddress) == 0)
		goto translate_return;
	p = _translatePage(m->page, linearAddress, hasAttribute);
	assert(p.value != INVALID_PAGE_ADDRESS);
	if(doReserve){
		int ok = addPhysicalBlockReference(m->physical, p.value);
		assert(ok);
	}
	translate_return:
	releaseLock(&bm->b.lock);
	return p;
}

PhysicalAddress checkAndTranslatePage(LinearMemoryManager *m, void *linearAddress){
	return checkAndTranslateBlock(m, (uintptr_t)linearAddress, 0, 0);
}

PhysicalAddress checkAndReservePage(LinearMemoryManager *m, void *linearAddress, PageAttribute hasAttribute){
	return checkAndTranslateBlock(m, (uintptr_t)linearAddress, hasAttribute, 1);
}
