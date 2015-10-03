#include"memory_private.h"
#include"buddy.h"

struct LinearMemoryBlockManager{
	int initialBlockCount, maxBlockCount;
	MemoryBlockManager b;
};

enum MemoryBlockStatus{
	MEMORY_FREE_OR_COVERED = 1, // may be free
	MEMORY_USING = 2, // must be using
	MEMORY_RELEASING = 3 // must be releasing
}__attribute__((__packed__));

typedef struct LinearMemoryBlock{
	size_t mappedSize;
	enum MemoryBlockStatus status;
	MemoryBlock block;
}LinearMemoryBlock;

static_assert(MEMBER_OFFSET(LinearMemoryBlockManager, b.blockArray) == sizeof(LinearMemoryBlockManager));

static void initLinearMemoryBlock(void *voidLMB){
	LinearMemoryBlock *lmb = voidLMB;
	lmb->mappedSize = 0;
	lmb->status = MEMORY_USING;
	initMemoryBlock(&lmb->block);
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

	if(getMaxBlockManagerSize(bm) > manageSize){
		panic("buddy memory manager (linear) initialization error");
	}
	return bm;
}

uintptr_t getLinearBeginAddress(LinearMemoryBlockManager *m){
	return getBeginAddress(&m->b);
}

size_t getAllocatedBlockSize(LinearMemoryBlockManager *m, uintptr_t address){
	LinearMemoryBlock *lmb = addressToElement(&m->b, address);
	assert(lmb->mappedSize != 0 && lmb->mappedSize % PAGE_SIZE == 0);
	return lmb->mappedSize;
}

const size_t minLinearBlockManagerSize = sizeof(LinearMemoryBlockManager);
const size_t maxLinearBlockManagerSize = sizeof(LinearMemoryBlockManager) +
	((0xffffffff / MIN_BLOCK_SIZE) + 1) * sizeof(LinearMemoryBlock);

size_t getMaxBlockManagerSize(LinearMemoryBlockManager *m){
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
	case MEMORY_RELEASING:
		return 0;
	default:
		assert(0);
		return 0;
	}
}

static void prepareReleaseBlock(LinearMemoryBlock *b){
	assert(b->status == MEMORY_USING);
	b->status = MEMORY_RELEASING;
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

uintptr_t allocateOrExtendLinearBlock(LinearMemoryManager *m, size_t *size, MemoryBlockFlags flags){
	LinearMemoryBlockManager *bm = m->linear;
	size_t l_size = *size;
	LinearMemoryBlock *lmb;
	MemoryBlock *block;
	acquireLock(&bm->b.lock);
	block = allocateBlock_noLock(&bm->b, &l_size, flags);
	if(block != NULL){ // ok
		goto alcOrExt_return;
	}
	int exCount = getExtendBlockCount(bm, *size);
	if(exCount == 0){ // error
		goto alcOrExt_return;
	}
	if(extendLinearBlock_noLock(m, exCount) == 0){ // error
		// printk("warning: extendLinearBlock failed");
		// goto alcOrExt_return;
	}
	l_size = *size;
	block = allocateBlock_noLock(&bm->b, &l_size, flags);
	//if(block != NULL){
	//	goto alcOrExt_return;
	//}
	alcOrExt_return:
	if(block != NULL){
		lmb = blockToElement(&bm->b, block);
		lmb->mappedSize = (*size);
		assert(lmb->status == MEMORY_FREE_OR_COVERED);
		lmb->status = MEMORY_USING;
	}
	releaseLock(&bm->b.lock);
	if(block == NULL){
		return UINTPTR_NULL;
	}

	(*size) = l_size;
	uintptr_t linearAddress = elementToAddress(&bm->b, lmb);

	return linearAddress;
}

void releaseLinearBlock(LinearMemoryBlockManager *m, uintptr_t address){
	acquireLock(&m->b.lock);
	LinearMemoryBlock *lmb = addressToElement(&m->b, address);
	assert(lmb->status == MEMORY_USING || lmb->status == MEMORY_RELEASING);
	lmb->status = MEMORY_FREE_OR_COVERED;
	releaseBlock_noLock(&m->b, &lmb->block);
	releaseLock(&m->b.lock);
}

int _checkAndUnmapLinearBlock(LinearMemoryManager *m, uintptr_t linearAddress, int releasePhysical){
	LinearMemoryBlockManager *bm = m->linear;
	int r;
	acquireLock(&bm->b.lock);
	r = isAddressInRange(&bm->b, linearAddress);
	if(r == 0){
		goto chkAndRls_return;
	}
	LinearMemoryBlock *lmb = addressToElement(&bm->b, linearAddress);
	r = isReleasableBlock_noLock(bm, lmb);
	if(r == 0){
		// r = 0;
		goto chkAndRls_return;
	}
	// IMPROVE: reduce getBlock

	size_t s = getAllocatedBlockSize(bm, linearAddress);
	if(s % PAGE_SIZE != 0){
		r = 0;
		panic("linear block must align to PAGE_SIZE");
		goto chkAndRls_return;
	}
	prepareReleaseBlock(lmb);
	// TODO: remove releasePhysical¡@parameter
	assert(lmb->block.flags == (unsigned)releasePhysical);
	releaseLock(&bm->b.lock);

	_unmapPage(m->page, m->physical, (void*)linearAddress, s, lmb->block.flags & WITH_PHYSICAL_PAGES_FLAG);

	acquireLock(&bm->b.lock);
	assert(lmb->status == MEMORY_USING || lmb->status == MEMORY_RELEASING);
	lmb->status = MEMORY_FREE_OR_COVERED;
	releaseBlock_noLock(&bm->b, &lmb->block);
	r = 1;
	chkAndRls_return:
	releaseLock(&bm->b.lock);
	return r;
}

// release every m->linear->block and reset m->linear->blockCount to initialBlockCount
// assume single thread
void releaseAllLinearBlocks(LinearMemoryManager *m){
	LinearMemoryBlockManager *bm = m->linear;
	int i = 0;//bm->initialBlockCount;
	while(i < bm->b.blockCount){
		LinearMemoryBlock *lmb =(LinearMemoryBlock*)indexToElement(&bm->b, i);
		assert(lmb->status != MEMORY_RELEASING);
		_checkAndUnmapLinearBlock(m, blockToAddress(&bm->b, &lmb->block), lmb->block.flags);
		// no lock
		// no matter the block is free, using, or covered, adding the block size does not skip any using block
		i += (1 << lmb->block.sizeOrder) / MIN_BLOCK_SIZE;
	}
	assert(i == bm->b.blockCount);
	// see allocateOrExtendLinearBlock
	uintptr_t rlsPageBegin = CEIL((uintptr_t)indexToElement(&bm->b, bm->initialBlockCount), PAGE_SIZE);
	uintptr_t rlsPageEnd = CEIL((uintptr_t)indexToElement(&bm->b, bm->b.blockCount), PAGE_SIZE);
	unmapPage_L(m->page, (void*)rlsPageBegin, rlsPageEnd - rlsPageBegin);
	resetBlockArray(&bm->b, bm->initialBlockCount, initLinearMemoryBlock);
}

static PhysicalAddress checkAndTranslateBlock(LinearMemoryManager *m, uintptr_t linearAddress){
	LinearMemoryBlockManager *bm = m->linear;
	PhysicalAddress p = {UINTPTR_NULL};
	acquireLock(&bm->b.lock);
	if(isAddressInRange(&bm->b, linearAddress)){
		if(isUsingBlock_noLock(bm, linearAddress)){
			p = translateExistingPage(m->page, (void*)linearAddress);
			assert(p.value != UINTPTR_NULL);
		}
	}
	releaseLock(&bm->b.lock);
	return p;
}

PhysicalAddress checkAndTranslatePage(LinearMemoryManager *m, void *linearAddress){
	return checkAndTranslateBlock(m, (uintptr_t)linearAddress);
}
