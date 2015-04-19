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
	size_t sizeOfThis;
	uintptr_t beginAddress;
	int blockCount;
	size_t freeSize;
	Spinlock lock;
	MemoryBlock *freeBlock[MAX_BLOCK_ORDER - MIN_BLOCK_ORDER + 1];
	MemoryBlock *block;
};

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

static int isInFreeList(MemoryBlock *mb){
	return mb->prev != NULL;
}

int isFreeBlock(MemoryBlockManager *m, void* memory){
	return isInFreeList(getBlock(m, (uintptr_t)memory));
}

uintptr_t getFirstBlockAddress(MemoryBlockManager *m){
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

void *allocateBlock(MemoryBlockManager *m, size_t size){
	void *r = NULL;
	acquireLock(&m->lock);
	if(size > MAX_BLOCK_SIZE){
		goto allocateBlock_return;
	}
	// assert(size >= MIN_BLOCK_SIZE);
	size_t i, i2;
	for(i = MIN_BLOCK_ORDER; ((size_t)1 << i) < size; i++);
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
	r = (void*)getAddress(m, b);
	allocateBlock_return:
	releaseLock(&m->lock);
	return r;
}

void releaseBlock(MemoryBlockManager *m, void *memory){
	acquireLock(&m->lock);
	MemoryBlock *b = getBlock(m, (uintptr_t)memory);
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
			assert((a1>a2? a1-a2: a2-a1) == ((uintptr_t)1 << b->sizeOrder));
		}
		#endif
		b = (getAddress(m, b) < getAddress(m, buddy)? b: buddy);
		b->sizeOrder++;
	}
	ADD_TO_DQUEUE(b, &m->freeBlock[b->sizeOrder - MIN_BLOCK_ORDER]);
	releaseLock(&m->lock);
}

size_t getMetaSize(MemoryBlockManager *m){
	return m->sizeOfThis;
}

int getBlockCount(MemoryBlockManager *m){
	return m->blockCount;
}

MemoryBlockManager *createMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t endAddr
){
	uintptr_t m = manageBase;
	assert(m % sizeof(uintptr_t) == 0);
	MemoryBlockManager *bm = (MemoryBlockManager*)m;
	m += sizeof(MemoryBlockManager);
	bm->freeSize = 0;
	assert(beginAddr % MIN_BLOCK_SIZE == 0);
	bm->beginAddress = beginAddr;

	bm->blockCount = (endAddr - bm->beginAddress) / MIN_BLOCK_SIZE;
	bm->lock = initialSpinlock;
	bm->block = (MemoryBlock*)m;
	m += sizeof(MemoryBlock) * bm->blockCount;
	bm->sizeOfThis = m - manageBase;
	if(bm->sizeOfThis > manageSize){
		panic("buddy memory manager initialization error");
	}
	int b;
	for(b = 0; b < bm->blockCount; b++){
		 // all blocks are using and in MIN_BLOCK_ORDER in the beginning
		initMemoryBlock(&bm->block[b]);
	}
	for(b = 0; b <= MAX_BLOCK_ORDER - MIN_BLOCK_ORDER; b++){
		bm->freeBlock[b] = NULL;
	}
	return bm;
}
