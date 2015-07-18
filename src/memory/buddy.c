#include<std.h>
#include"common.h"
#include"memory.h"
#include"memory_private.h"
#include"multiprocessor/spinlock.h"

// BlockManager functions
typedef struct MemoryBlock{
	unsigned sizeOrder;
	struct MemoryBlock**prev, *next;
}MemoryBlock;

struct MemoryBlockManager{
	uintptr_t beginAddress;
	int blockCount, maxBlockCount;
	size_t freeSize;
	Spinlock lock;
	MemoryBlock *freeBlock[MAX_BLOCK_ORDER - MIN_BLOCK_ORDER + 1];
	MemoryBlock block[0];
};

static_assert(sizeof(*((MemoryBlockManager*)NULL)->block) == sizeof(MemoryBlock));

static void initMemoryBlock(MemoryBlock *mb){
	mb->sizeOrder = MIN_BLOCK_ORDER;
	mb->next = NULL;
	mb->prev = NULL;
}

static uintptr_t getAddress(MemoryBlockManager *m, MemoryBlock *mb){
	return (((uintptr_t)(mb - (m->block))) << MIN_BLOCK_ORDER) + m->beginAddress;
}

static MemoryBlock *getBlock(MemoryBlockManager *m, uintptr_t address){
	assert(address % MIN_BLOCK_SIZE == 0);
	assert(address >= m->beginAddress);
	int i = (address - m->beginAddress) / MIN_BLOCK_SIZE;
	assert(i >= 0 && i < m->blockCount);
	return &m->block[i];
}

uintptr_t getBlockAddress(MemoryBlockManager *m, uintptr_t address){
	assert(address % MIN_BLOCK_SIZE == 0);
	assert(address >= m->beginAddress);
	int i = (address - m->beginAddress) / MIN_BLOCK_SIZE;
	// maxBlockCount is acceptable
	assert(i >= 0 && i <= m->maxBlockCount);
	return (uintptr_t)(m->block + i);
}

static int isInFreeList(MemoryBlock *mb){
	return mb->prev != NULL;
}

uintptr_t getBeginAddress(MemoryBlockManager *m){
	return m->beginAddress;
}

static MemoryBlock *getBuddy(MemoryBlockManager *m, MemoryBlock *b){
	int index = (b - m->block);
	int buddy = (index ^ (1 << (b->sizeOrder - MIN_BLOCK_ORDER)));
	if(buddy >= m->blockCount){
		return NULL;
	}
	return m->block + buddy;
}

uintptr_t allocateBlock(MemoryBlockManager *m, size_t *size){
	uintptr_t r = INVALID_BLOCK_ADDRESS;
	acquireLock(&m->lock);
	if(*size > MAX_BLOCK_SIZE){
		goto allocateBlock_return;
	}
	// assert(size >= MIN_BLOCK_SIZE);
	size_t i, i2;
	for(i = MIN_BLOCK_ORDER; ((size_t)1 << i) < (*size); i++);
	for(i2 = i; 1; i2++){
		if(m->freeBlock[i2 - MIN_BLOCK_ORDER] != NULL)
			break;
		if(i2 == MAX_BLOCK_ORDER)
			goto allocateBlock_return;
	}
	MemoryBlock *const b = m->freeBlock[i2 - MIN_BLOCK_ORDER];
	REMOVE_FROM_DQUEUE(b);
	while(i2 != i){
		// split b and get buddy
		b->sizeOrder--;
		MemoryBlock *b2 = getBuddy(m, b);
		assert(b2 != NULL);
		assert(isInFreeList(b2) == 0 && b2->sizeOrder == b->sizeOrder);
		ADD_TO_DQUEUE(b2, &m->freeBlock[b2->sizeOrder - MIN_BLOCK_ORDER]);
		i2--;
	}
	m->freeSize -= (1 << i);
	(*size) = (size_t)(1 << i);
	r = getAddress(m, b);
	allocateBlock_return:
	releaseLock(&m->lock);
	return r;
}

size_t getAllocatedBlockSize(MemoryBlockManager *m, uintptr_t address){
	MemoryBlock *b = getBlock(m, address);
	return (1 << b->sizeOrder);
}

int isReleasableBlock(MemoryBlockManager *m, uintptr_t address){
	if(address % MIN_BLOCK_SIZE != 0)
		return 0;
	if(address < m->beginAddress)
		return 0;
	if((address - m->beginAddress) / MIN_BLOCK_SIZE >= (unsigned)m->blockCount)
		return 0;
	MemoryBlock *b1 = getBlock(m, address);
	if(isInFreeList(b1))
		return 0;
	MemoryBlock *b2 = getBuddy(m, b1);
	if(b2 == NULL)
		return 1;
	// b2 < b1 and range of b2 includes b1
	if(b2->sizeOrder > b1->sizeOrder){
		assert((uintptr_t)b2 < (uintptr_t)b1);
		return 0;
	}
	return 1;
}

void releaseBlock(MemoryBlockManager *m, uintptr_t address){
	acquireLock(&m->lock);
	MemoryBlock *b = getBlock(m, address);
	m->freeSize += (1 << b->sizeOrder);
	assert(isInFreeList(b) == 0);
	while(b->sizeOrder < MAX_BLOCK_ORDER){
		MemoryBlock *buddy = getBuddy(m, b);
		if(buddy == NULL)
			break;
		assert(buddy->sizeOrder <= b->sizeOrder);
		if(isInFreeList(buddy) == 0 || buddy->sizeOrder != b->sizeOrder)
			break;
		// merge
		//printk("%d %d\n",buddy->sizeOrder, b->sizeOrder);
		REMOVE_FROM_DQUEUE(buddy);
		#ifndef NDEBUG
		{
			uintptr_t a1 = (uintptr_t)getAddress(m,buddy), a2=(uintptr_t)getAddress(m,b);
			assert((a1 > a2? a1-a2: a2-a1) == ((uintptr_t)1 << b->sizeOrder));
		}
		#endif
		b = (getAddress(m, b) < getAddress(m, buddy)? b: buddy);
		b->sizeOrder++;
	}
	ADD_TO_DQUEUE(b, &m->freeBlock[b->sizeOrder - MIN_BLOCK_ORDER]);
	releaseLock(&m->lock);
}

const size_t minBlockManagerSize = sizeof(MemoryBlockManager);
const size_t maxBlockManagerSize = sizeof(MemoryBlockManager) +
	((0xffffffff / MIN_BLOCK_SIZE) + 1) * sizeof(MemoryBlock);

size_t getBlockManagerSize(MemoryBlockManager *m){
	return sizeof(*m) + m->maxBlockCount * sizeof(*m->block);
}

int getBlockCount(MemoryBlockManager *m){
	return m->blockCount;
}

int getMaxBlockCount(MemoryBlockManager *m){
	return m->maxBlockCount;
}

int extendBlockCount(MemoryBlockManager *m, int addBlockCount){
	acquireLock(&m->lock);
	if(addBlockCount < 0 || addBlockCount > m->maxBlockCount - m->blockCount){
		releaseLock(&m->lock);
		return 0;
	}
	int i;
	for(i = m->blockCount; i < m->blockCount + addBlockCount; i++){
		initMemoryBlock(&m->block[i]);
	}
	m->blockCount += addBlockCount;
	releaseLock(&m->lock);
	return 1;
}

int getFreeBlockSize(MemoryBlockManager *m){
	return m->freeSize;
}

MemoryBlockManager *createMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t endAddr,
	uintptr_t maxEndAddr
){
	assert(manageBase % sizeof(uintptr_t) == 0);
	MemoryBlockManager *bm = (MemoryBlockManager*)manageBase;
	bm->lock = initialSpinlock;
	bm->freeSize = 0;
	assert(beginAddr % MIN_BLOCK_SIZE == 0);
	assert(INVALID_BLOCK_ADDRESS < beginAddr || INVALID_BLOCK_ADDRESS >= maxEndAddr);
	bm->beginAddress = beginAddr;

	bm->maxBlockCount = (maxEndAddr - bm->beginAddress) / MIN_BLOCK_SIZE;
	bm->blockCount = 0;
	if(getBlockManagerSize(bm) > manageSize){
		panic("buddy memory manager initialization error");
		return bm == NULL? ((void*)0xffffffff): NULL;
	}
	extendBlockCount(bm, (endAddr - bm->beginAddress) / MIN_BLOCK_SIZE);
	int i;
	for(i = 0; i <= MAX_BLOCK_ORDER - MIN_BLOCK_ORDER; i++){
		bm->freeBlock[i] = NULL;
	}

	return bm;
}
