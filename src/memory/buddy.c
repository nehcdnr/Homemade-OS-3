#include<std.h>
#include"common.h"
#include"memory.h"
#include"memory_private.h"
#include"assembly/assembly.h"
#include"multiprocessor/spinlock.h"

enum MemoryBlockStatus{
	MEMORY_FREE_OR_COVERED = 1, // may be free
	MEMORY_USING = 2, // must be using
	MEMORY_RELEASING = 3 // must be releasing
}__attribute__((__packed__));
// BlockManager functions

typedef uint16_t sizeorder_t;

typedef struct MemoryBlock{
	sizeorder_t sizeOrder;
	enum MemoryBlockStatus status;
	// for linear memory,
	// this value is indicates whether the linear block is mapped to physical pages.
	// for physical memory, it is 0
	uint8_t flags: 8;
	struct MemoryBlock**prev, *next;
}MemoryBlock;

typedef struct LinearMemoryBlock{
	size_t mappedSize;
	MemoryBlock block;
}LinearMemoryBlock;

static_assert(sizeof(MemoryBlock) == 12);

struct MemoryBlockManager{
	Spinlock lock;
	uintptr_t beginAddress;
	size_t blockStructSize;
	size_t blockStructOffset;
	int blockCount;
	size_t freeSize;
	MemoryBlock *freeBlock[MAX_BLOCK_ORDER - MIN_BLOCK_ORDER + 1];

	uint8_t blockArray[0];
};

static_assert((uintptr_t)(((MemoryBlockManager*)0)->blockArray) == sizeof(MemoryBlockManager));

struct LinearMemoryBlockManager{
	int initialBlockCount, maxBlockCount;
	MemoryBlockManager b;
};

static_assert((uintptr_t)(((LinearMemoryBlockManager*)0)->b.blockArray) == sizeof(LinearMemoryBlockManager));


static void initMemoryBlock(MemoryBlock *voidMB){
	MemoryBlock *mb = voidMB;
	mb->sizeOrder = MIN_BLOCK_ORDER;
	mb->status = MEMORY_USING;
	mb->flags = 0;
	mb->next = NULL;
	mb->prev = NULL;
}

static void initLinearMemoryBlock(void *voidLMB){
	LinearMemoryBlock *lmb = voidLMB;
	lmb->mappedSize = 0;
	initMemoryBlock(&lmb->block);
}

static int elementToIndex(MemoryBlockManager *m, const MemoryBlock *block){
	return (int)(((uintptr_t)block) - m->blockStructOffset - (uintptr_t)m->blockArray) / m->blockStructSize;
}

static void *indexToElement(MemoryBlockManager *m, int index){
	return (void*)(((uintptr_t)m->blockArray) + m->blockStructSize * index);
}

static LinearMemoryBlock *toLinearBlock(LinearMemoryBlockManager *m, MemoryBlock *b){
#ifndef NDEBUG
	int i = elementToIndex(&m->b, b);
	assert(i >= 0 && i < m->b.blockCount);
#endif
	return (LinearMemoryBlock*)(((uintptr_t)b) - m->b.blockStructOffset);
}

static uintptr_t getAddress(MemoryBlockManager *m, MemoryBlock *mb){
	return (((uintptr_t)elementToIndex(m, mb)) << MIN_BLOCK_ORDER) + m->beginAddress;
}

static int isInRange(MemoryBlockManager *m, uintptr_t address){
	if(address % MIN_BLOCK_SIZE != 0)
		return 0;
	if(address < m->beginAddress)
		return 0;
	if((address - m->beginAddress) / MIN_BLOCK_SIZE >= (unsigned)m->blockCount)
		return 0;
	return 1;
}

static int addressToIndex(MemoryBlockManager *m, uintptr_t address){
	return (address - m->beginAddress) / MIN_BLOCK_SIZE;
}

uintptr_t getBeginAddress(MemoryBlockManager *m){
	return m->beginAddress;
}

uintptr_t getLinearBeginAddress(LinearMemoryBlockManager *m){
	return getBeginAddress(&m->b);
}

// assume locked
static MemoryBlock *getBlock(MemoryBlockManager *m, uintptr_t address){
	assert(isInRange(m, address));
	int i = addressToIndex(m, address);
	return (MemoryBlock*)(((uintptr_t)indexToElement(m, i)) + m->blockStructOffset);
}

// assume locked
static MemoryBlock *getBuddy(MemoryBlockManager *m, const MemoryBlock *b){
	assert(isAcquirable(&m->lock) == 0);
	int index = elementToIndex(m, b);
	int buddy = (index ^ (1 << (b->sizeOrder - MIN_BLOCK_ORDER)));
	if(buddy >= m->blockCount){
		return NULL;
	}
	return (MemoryBlock*)(((uintptr_t)indexToElement(m, buddy)) + m->blockStructOffset);
}

static size_t ceilAllocateOrder(size_t s){
	if(s > MAX_BLOCK_SIZE)
		return MAX_BLOCK_ORDER + 1;
	size_t i;
	for(i = MIN_BLOCK_ORDER; (((size_t)1) << i) < s; i++);
	return i;
}

static MemoryBlock *allocateBlock_noLock(MemoryBlockManager *m, size_t *size, MemoryBlockFlags flags){
	assert(isAcquirable(&m->lock) == 0);
	// assert(size >= MIN_BLOCK_SIZE);
	size_t i = ceilAllocateOrder(*size), i2;
	if(i > MAX_BLOCK_ORDER){
		return NULL;
	}
	for(i2 = i; 1; i2++){
		if(m->freeBlock[i2 - MIN_BLOCK_ORDER] != NULL)
			break;
		if(i2 == MAX_BLOCK_ORDER){
			return NULL;
		}
	}
	MemoryBlock *const b = m->freeBlock[i2 - MIN_BLOCK_ORDER];
	REMOVE_FROM_DQUEUE(b);
	assert(b->status == MEMORY_FREE_OR_COVERED);
	b->status = MEMORY_USING;
	b->flags = flags;
	while(i2 != i){
		// split b and get buddy
		b->sizeOrder--;
		MemoryBlock *b2 = getBuddy(m, b);
		assert(b2 != NULL);
		assert(IS_IN_DQUEUE(b2) == 0 && b2->sizeOrder == b->sizeOrder);
		assert(b2->status == MEMORY_FREE_OR_COVERED);
		ADD_TO_DQUEUE(b2, &m->freeBlock[b2->sizeOrder - MIN_BLOCK_ORDER]);
		i2--;
	}
	m->freeSize -= (1 << i);
	(*size) = (size_t)(1 << i);
	return b;
}

uintptr_t allocateBlock(MemoryBlockManager *m, size_t *size, MemoryBlockFlags flags){
	acquireLock(&m->lock);
	MemoryBlock *b = allocateBlock_noLock(m, size, flags);
	releaseLock(&m->lock);
	uintptr_t r = (b == NULL? UINTPTR_NULL: getAddress(m, b));
	return r;
}

size_t getAllocatedBlockSize(LinearMemoryBlockManager *m, uintptr_t address){
	int i = addressToIndex(&m->b, address);
	LinearMemoryBlock *lb = indexToElement(&m->b, i);
	assert(lb->mappedSize != 0 && lb->mappedSize % PAGE_SIZE == 0);
	return lb->mappedSize;
}

static int isReleasableBlock_noLock(MemoryBlockManager *m, MemoryBlock *b1){
#ifndef NDEBUG
	MemoryBlock *b2 = getBuddy(m, b1);
#endif
	switch(b1->status){
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

static int isReleasableAddress_noLock(MemoryBlockManager *m, uintptr_t address){
	if(isInRange(m, address) == 0){
		return 0;
	}
	return isReleasableBlock_noLock(m, getBlock(m, address));
}

int isReleasableAddress(MemoryBlockManager *m, uintptr_t address){
	int r = 0;
	acquireLock(&m->lock);
	r = isReleasableAddress_noLock(m, address);
	releaseLock(&m->lock);
	return r;
}

static void prepareReleaseBlock(MemoryBlock *b){
	assert(b->status == MEMORY_USING);
	b->status = MEMORY_RELEASING;
}

static void releaseBlock_noLock(MemoryBlockManager *m, MemoryBlock *b){
	m->freeSize += (1 << b->sizeOrder);
	assert(IS_IN_DQUEUE(b) == 0);
	assert(b->status == MEMORY_USING || b->status == MEMORY_RELEASING);
	b->status = MEMORY_FREE_OR_COVERED;
	b->flags = 0;
	while(b->sizeOrder < MAX_BLOCK_ORDER){
		MemoryBlock *buddy = getBuddy(m, b);
		if(buddy == NULL)
			break;
		assert(buddy->sizeOrder <= b->sizeOrder);
		if(IS_IN_DQUEUE(buddy) == 0 /*not free*/|| buddy->sizeOrder != b->sizeOrder/*partial free*/){
			break;
		}
		// merge
		//printk("%d %d\n",buddy->sizeOrder, b->sizeOrder);
		assert(buddy->status == MEMORY_FREE_OR_COVERED);
		REMOVE_FROM_DQUEUE(buddy);
		#ifndef NDEBUG
		{
			uintptr_t a1 = (uintptr_t)getAddress(m,buddy), a2=(uintptr_t)getAddress(m,b);
			assert((a1 > a2? a1-a2: a2-a1) == (size_t)(1 << b->sizeOrder));
		}
		#endif
		b = (getAddress(m, b) < getAddress(m, buddy)? b: buddy);
		b->sizeOrder++;
	}
	ADD_TO_DQUEUE(b, &m->freeBlock[b->sizeOrder - MIN_BLOCK_ORDER]);
}

void releaseBlock(MemoryBlockManager *m, uintptr_t address){
	acquireLock(&m->lock);
	assert(isReleasableAddress_noLock(m, address));
	releaseBlock_noLock(m, getBlock(m, address));
	releaseLock(&m->lock);
}

// if MEMORY_RELEASING or MEMORY_FREE, return 0
// if MEMORY_USING, return 1
static int isUsingBlock_noLock(MemoryBlockManager *m, uintptr_t address){
	const MemoryBlock *b1, *b2;
	b1 = getBlock(m, address);
	while(1){
		b2 = getBuddy(m, b1);
		// b1 is not covered by other blocks
		if(b2 == NULL || b2->sizeOrder <= b1->sizeOrder){
			break;
		}
		assert(b2 < b1);
		b1 = b2;
	}
	return (b1->status == MEMORY_USING);
}

//const size_t minBlockManagerSize = sizeof(MemoryBlockManager);
//const size_t maxBlockManagerSize = sizeof(MemoryBlockManager) +
//	((0xffffffff / MIN_BLOCK_SIZE) + 1) * sizeof(MemoryBlock);
const size_t minLinearBlockManagerSize = sizeof(LinearMemoryBlockManager);
const size_t maxLinearBlockManagerSize = sizeof(LinearMemoryBlockManager) +
	((0xffffffff / MIN_BLOCK_SIZE) + 1) * sizeof(LinearMemoryBlock);
size_t getBlockManagerSize(MemoryBlockManager *m){
	return sizeof(*m) + m->blockCount * m->blockStructSize;
}

size_t getMaxBlockManagerSize(LinearMemoryBlockManager *m){
	return sizeof(*m) + m->maxBlockCount * m->b.blockStructSize;
}

int getBlockCount(MemoryBlockManager *m){
	return m->blockCount;
}

int getMaxBlockCount(LinearMemoryBlockManager *m){
	return m->maxBlockCount;
}

int getFreeBlockSize(MemoryBlockManager *m){
	return m->freeSize;
}

int getFreeLinearBlockSize(LinearMemoryBlockManager *m){
	return m->b.freeSize;
}

typedef void(*InitMemoryBlockFunction)(void*);

static void resetBlockArray(MemoryBlockManager *bm, int initialBlockCount, InitMemoryBlockFunction initBlockFunc){
	int i;
	bm->blockCount = initialBlockCount;
	bm->freeSize = 0;
	for(i = 0; i < bm->blockCount; i++){
		initBlockFunc((void*)(((uintptr_t)bm->blockArray) + i * bm->blockStructSize));
	}
	for(i = 0; i <= MAX_BLOCK_ORDER - MIN_BLOCK_ORDER; i++){
		bm->freeBlock[i] = NULL;
	}
}

static void initMemoryBlockManager(
	MemoryBlockManager *bm,
	size_t blockStructSize,
	size_t blockStructOffset,
	uintptr_t beginAddr,
	uintptr_t endAddr,
	InitMemoryBlockFunction initBlockFunc
){
	assert(beginAddr % MIN_BLOCK_SIZE == 0);
	//assert(UINTPTR_NULL < beginAddr || UINTPTR_NULL >= maxEndAddr);
	bm->lock = initialSpinlock;
	bm->blockStructSize = blockStructSize;
	bm->blockStructOffset = blockStructOffset;
	bm->beginAddress = beginAddr;
	resetBlockArray(bm, (endAddr - bm->beginAddress) / MIN_BLOCK_SIZE, initBlockFunc);
}

MemoryBlockManager *createMemoryBlockManager(
	uintptr_t manageBase, size_t manageSize,
	uintptr_t beginAddr, uintptr_t initEndAddr
){
	assert(manageBase % sizeof(uintptr_t) == 0);
	MemoryBlockManager *bm = (MemoryBlockManager*)manageBase;
	initMemoryBlockManager(
		bm, sizeof(MemoryBlock), 0,
		beginAddr, initEndAddr,
		(InitMemoryBlockFunction)initMemoryBlock
	);
	if(getBlockManagerSize(bm) > manageSize){
		panic("buddy memory manager initialization error");
	}
	return bm;
}

// linear memory manager

LinearMemoryBlockManager *createLinearBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr,
	uintptr_t maxEndAddr
){
	LinearMemoryBlockManager *bm = (LinearMemoryBlockManager*)manageBase;
	initMemoryBlockManager(
		&bm->b, sizeof(LinearMemoryBlock), ((size_t)&((LinearMemoryBlock*)0)->block),
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

static int extendLinearBlock(LinearMemoryManager *m, int exCount){
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
		releaseBlock_noLock(&bm->b, &lastBlock->block);
	}
	return bm->b.blockCount >= newBlockCount;
}

uintptr_t allocateOrExtendLinearBlock(LinearMemoryManager *m, size_t *size, MemoryBlockFlags flags){
	LinearMemoryBlockManager *bm = m->linear;
	size_t l_size = *size;
	acquireLock(&bm->b.lock);
	MemoryBlock *block = allocateBlock_noLock(&bm->b, &l_size, flags);
	if(block != NULL){ // ok
		goto alcOrExt_return;
	}
	int exCount = getExtendBlockCount(bm, *size);
	if(exCount == 0){ // error
		goto alcOrExt_return;
	}
	if(extendLinearBlock(m, exCount) == 0){ // error
		// printk("warning: extendLinearBlock failed");
		// goto alcOrExt_return;
	}
	l_size = *size;
	block = allocateBlock_noLock(&bm->b, &l_size, flags);
	//if(block != NULL){
	//	goto alcOrExt_return;
	//}
	alcOrExt_return:
	releaseLock(&bm->b.lock);
	if(block == NULL){
		return UINTPTR_NULL;
	}
	toLinearBlock(bm, block)->mappedSize = (*size);
	(*size) = l_size;
	uintptr_t linearAddress = getAddress(&bm->b, block);

	return linearAddress;
}

void releaseLinearBlock(LinearMemoryBlockManager *m, uintptr_t address){
	releaseBlock(&m->b, address);
}

int _checkAndUnmapLinearBlock(LinearMemoryManager *m, uintptr_t linearAddress, int releasePhysical){
	LinearMemoryBlockManager *bm = m->linear;

	int r;
	size_t s;
	MemoryBlock *b;
	acquireLock(&bm->b.lock);
	r = isReleasableAddress_noLock(&bm->b, linearAddress);
	if(r == 0){
		// r = 0;
		goto chkAndRls_return;
	}
	// IMPROVE: reduce getBlock
	b = getBlock(&bm->b, linearAddress);
	s = getAllocatedBlockSize(bm, linearAddress);
	if(s % PAGE_SIZE != 0){
		r = 0;
		panic("linear block must align to PAGE_SIZE");
		goto chkAndRls_return;
	}
	prepareReleaseBlock(b);
	// TODO: remove releasePhysical¡@parameter
	assert(b->flags == (unsigned)releasePhysical);
	releaseLock(&bm->b.lock);

	_unmapPage(m->page, m->physical, (void*)linearAddress, s, b->flags & WITH_PHYSICAL_PAGES_FLAG);

	acquireLock(&bm->b.lock);
	releaseBlock_noLock(&bm->b, getBlock(&bm->b, linearAddress));
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
		assert(lmb->block.status != MEMORY_RELEASING);
		_checkAndUnmapLinearBlock(m, getAddress(&bm->b, &lmb->block), lmb->block.flags);
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
	if(isInRange(&bm->b, linearAddress) == 0){
		return p;
	}
	acquireLock(&bm->b.lock);
	if(isUsingBlock_noLock(&bm->b, linearAddress)){
		p = translateExistingPage(m->page, (void*)linearAddress);
		assert(p.value != UINTPTR_NULL);
	}
	releaseLock(&bm->b.lock);
	return p;
}

PhysicalAddress checkAndTranslatePage(LinearMemoryManager *m, void *linearAddress){
	return checkAndTranslateBlock(m, (uintptr_t)linearAddress);
}
