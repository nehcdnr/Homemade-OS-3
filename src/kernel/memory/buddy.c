#include<std.h>
#include"common.h"
#include"memory.h"
#include"memory_private.h"
#include"buddy.h"
#include"assembly/assembly.h"
#include"multiprocessor/spinlock.h"

static_assert(sizeof(MemoryBlock) == 12);
static_assert(MEMBER_OFFSET(MemoryBlockManager, blockArray) == sizeof(MemoryBlockManager));

void initMemoryBlock(MemoryBlock *voidMB){
	MemoryBlock *mb = voidMB;
	mb->sizeOrder = MIN_BLOCK_ORDER;
	mb->flags = 0;
	mb->next = NULL;
	mb->prev = NULL;
}

// address(uintptr_t) - index(int) - element(void*) - block(MemoryBlock*)

static int addressToIndex(MemoryBlockManager *m, uintptr_t address){
	assert(isAddressInRange(m, address));
	return (address - m->beginAddress) / MIN_BLOCK_SIZE;
}

static uintptr_t indexToAddress(MemoryBlockManager *m, int index){
	return m->beginAddress + index * MIN_BLOCK_SIZE;
}

void *indexToElement(MemoryBlockManager *m, int index){
	return (void*)(((uintptr_t)m->blockArray) + index * m->blockStructSize);
}

static int elementToIndex(MemoryBlockManager *m, void *element){
	return (((uintptr_t)element) - (uintptr_t)m->blockArray) / m->blockStructSize;
}

static MemoryBlock *elementToBlock(MemoryBlockManager *m, void *element){
	return (MemoryBlock*)(((uintptr_t)element) + m->blockStructOffset);
}

void *blockToElement(MemoryBlockManager *m, const MemoryBlock *block){
	return (void*)(((uintptr_t)block) - m->blockStructOffset);
}

// compound translation
static int blockToIndex(MemoryBlockManager *m, const MemoryBlock *mb){
	return elementToIndex(m, blockToElement(m, mb));
}

void *addressToElement(MemoryBlockManager *m, uintptr_t address){
	return indexToElement(m, addressToIndex(m, address));
}

uintptr_t elementToAddress(MemoryBlockManager *m, void *element){
	return blockToAddress(m, elementToBlock(m, element));
}

uintptr_t blockToAddress(MemoryBlockManager *m, MemoryBlock *mb){
	return indexToAddress(m, blockToIndex(m, mb));
}

MemoryBlock *addressToBlock(MemoryBlockManager *m, uintptr_t address){
	return elementToBlock(m, addressToElement(m, address));
}

// assume locked
MemoryBlock *getBuddy(MemoryBlockManager *m, const MemoryBlock *b){
	assert(isAcquirable(&m->lock) == 0);
	int index = blockToIndex(m, b);
	int buddy = (index ^ (1 << (b->sizeOrder - MIN_BLOCK_ORDER)));
	if(buddy >= m->blockCount){
		return NULL;
	}
	return (MemoryBlock*)(((uintptr_t)indexToElement(m, buddy)) + m->blockStructOffset);
}

uintptr_t getBeginAddress(MemoryBlockManager *m){
	return m->beginAddress;
}

size_t ceilAllocateOrder(size_t s){
	if(s > MAX_BLOCK_SIZE)
		return MAX_BLOCK_ORDER + 1;
	size_t i;
	for(i = MIN_BLOCK_ORDER; (((size_t)1) << i) < s; i++);
	return i;
}

int isAddressInRange(MemoryBlockManager *m, uintptr_t address){
	if(address % MIN_BLOCK_SIZE != 0)
		return 0;
	if(address < m->beginAddress)
		return 0;
	if((address - m->beginAddress) / MIN_BLOCK_SIZE >= (unsigned)m->blockCount)
		return 0;
	return 1;
}

MemoryBlock *allocateBlock_noLock(MemoryBlockManager *m, size_t *size, MemoryBlockFlags flags){
	assert(isAcquirable(&m->lock) == 0);
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
	b->flags = flags;
	while(i2 != i){
		// split b and get buddy
		b->sizeOrder--;
		MemoryBlock *b2 = getBuddy(m, b);
		assert(b2 != NULL);
		assert(IS_IN_DQUEUE(b2) == 0 && b2->sizeOrder == b->sizeOrder);
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
	uintptr_t r = (b == NULL? UINTPTR_NULL: blockToAddress(m, b));
	return r;
}

void releaseBlock_noLock(MemoryBlockManager *m, MemoryBlock *b){
	m->freeSize += (1 << b->sizeOrder);
	assert(IS_IN_DQUEUE(b) == 0);
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
		REMOVE_FROM_DQUEUE(buddy);
#ifndef NDEBUG
			uintptr_t a1 = blockToAddress(m, b), a2 = blockToAddress(m, buddy);
			assert((a1 > a2? a1 - a2: a2 - a1) == (uintptr_t)(1 << b->sizeOrder));
#endif
		b = (((uintptr_t)b) < ((uintptr_t)buddy)? b: buddy);
		b->sizeOrder++;
	}
	ADD_TO_DQUEUE(b, &m->freeBlock[b->sizeOrder - MIN_BLOCK_ORDER]);
}

void releaseBlock(MemoryBlockManager *m, uintptr_t address){
	acquireLock(&m->lock);
	releaseBlock_noLock(m, addressToBlock(m, address));
	releaseLock(&m->lock);
}

//const size_t minBlockManagerSize = sizeof(MemoryBlockManager);
//const size_t maxBlockManagerSize = sizeof(MemoryBlockManager) +
//	((0xffffffff / MIN_BLOCK_SIZE) + 1) * sizeof(MemoryBlock);

size_t getBlockManagerSize(MemoryBlockManager *m){
	return sizeof(*m) + m->blockCount * m->blockStructSize;
}

int getBlockCount(MemoryBlockManager *m){
	return m->blockCount;
}

size_t getFreeBlockSize(MemoryBlockManager *m){
	return m->freeSize;
}

void resetBlockArray(MemoryBlockManager *bm, int initialBlockCount, InitMemoryBlockFunction initBlockFunc){
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

void initMemoryBlockManager(
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
