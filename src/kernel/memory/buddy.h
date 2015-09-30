#include<common.h>
#include"memory_private.h"
#include"multiprocessor/spinlock.h"

typedef uint16_t sizeorder_t;

typedef struct MemoryBlock{
	sizeorder_t sizeOrder;
	// for linear memory,
	// this value indicates whether the linear block is mapped to physical pages.
	// for physical memory, it is 0
	uint16_t flags: 16;
	struct MemoryBlock**prev, *next;
}MemoryBlock;

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

void initMemoryBlock(MemoryBlock *voidMB);
int elementToIndex(MemoryBlockManager *m, const MemoryBlock *block);
void *indexToElement(MemoryBlockManager *m, int index);
int addressToIndex(MemoryBlockManager *m, uintptr_t address);

uintptr_t blockToAddress(MemoryBlockManager *m, MemoryBlock *mb);
MemoryBlock *addressToBlock(MemoryBlockManager *m, uintptr_t address);
MemoryBlock *getBuddy(MemoryBlockManager *m, const MemoryBlock *b);

size_t ceilAllocateOrder(size_t s);

int isBlockInRange(MemoryBlockManager *m, uintptr_t address);

MemoryBlock *allocateBlock_noLock(MemoryBlockManager *m, size_t *size, MemoryBlockFlags flags);
void releaseBlock_noLock(MemoryBlockManager *m, MemoryBlock *b);

typedef void(*InitMemoryBlockFunction)(void*);

void resetBlockArray(MemoryBlockManager *bm, int initialBlockCount, InitMemoryBlockFunction initBlockFunc);

void initMemoryBlockManager(
	MemoryBlockManager *bm,
	size_t blockStructSize,
	size_t blockStructOffset,
	uintptr_t beginAddr,
	uintptr_t endAddr,
	InitMemoryBlockFunction initBlockFunc
);
