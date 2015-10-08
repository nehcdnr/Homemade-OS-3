#ifndef MEMORY_PRIVATE_H_INCLUDED
#define MEMORY_PRIVATE_H_INCLUDED

#include<std.h>
#include"memory.h"

// physicalblock.c
typedef struct PhysicalMemoryBlockManager PhysicalMemoryBlockManager;

PhysicalMemoryBlockManager *createPhysicalMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr
);

size_t getPhysicalBlockManagerSize(PhysicalMemoryBlockManager *m);
int getPhysicalBlockCount(PhysicalMemoryBlockManager *m);
size_t getFreePhysicalBlockSize(PhysicalMemoryBlockManager *m);
uintptr_t getPhysicalBeginAddress(PhysicalMemoryBlockManager *m);

// change reference count from 0 to 1
uintptr_t allocatePhysicalBlock(PhysicalMemoryBlockManager *m, size_t *size);
// if referenceCount == MAX_REFERENCE_COUNT, do not increase it and return 0
// otherwise, increase and return 1
int addPhysicalBlockReference(PhysicalMemoryBlockManager *m, uintptr_t address);
// subtract referenceCount and return the new value
// if the new value == 0, release the block
void releasePhysicalBlock(PhysicalMemoryBlockManager *m, uintptr_t address);

//linearblock.c
typedef struct LinearMemoryBlockManager LinearMemoryBlockManager;

LinearMemoryBlockManager *createLinearBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr,
	uintptr_t maxEndAddr
);

extern const size_t minLinearBlockManagerSize;
extern const size_t maxLinearBlockManagerSize;

size_t getMaxBlockManagerSize(LinearMemoryBlockManager *m);
int getMaxBlockCount(LinearMemoryBlockManager *m);
size_t getFreeLinearBlockSize(LinearMemoryBlockManager *m);
uintptr_t getLinearBeginAddress(LinearMemoryBlockManager *m);

size_t getAllocatedBlockSize(LinearMemoryBlockManager *m, uintptr_t address);
// release linear blocks only
void releaseLinearBlock(LinearMemoryBlockManager *m, uintptr_t address);

// allocate linear blocks only
uintptr_t allocateLinearBlock(LinearMemoryManager *m, size_t *size);
void commitAllocatingLinearBlock(LinearMemoryManager *m, uintptr_t linearAddress);
// release linear blocks, pages, and physical blocks
int checkAndReleaseLinearBlock(LinearMemoryManager *m, uintptr_t linearAddress);
void releaseAllLinearBlocks(LinearMemoryManager *m);

// 4K~1G
// block is always aligned to MIN_BLOCK_SIZE
#define MIN_BLOCK_ORDER (12)
#define MIN_BLOCK_SIZE (1<<MIN_BLOCK_ORDER)
#define MAX_BLOCK_ORDER (30)
#define MAX_BLOCK_SIZE (1<<MAX_BLOCK_ORDER)

// page.c
PageManager *initKernelPageTable(
	uintptr_t manageBase,
	uintptr_t manageBegin,
	uintptr_t manageEnd,
	uintptr_t kernelLinearBase,
	uintptr_t kernelLinearEnd
);

// assume arguments are valid
int _mapExistingPages_L(
		PhysicalMemoryBlockManager *physical, PageManager *dst, PageManager *src,
	void *dstLinear, uintptr_t srcLinear, size_t size,
	PageAttribute attribute, PageAttribute srcHasAttribute
);

PhysicalAddress _translatePage(PageManager *p, uintptr_t linearAddress, PageAttribute hasAtribute);

// linear + physical + page
struct LinearMemoryManager{
	PhysicalMemoryBlockManager *physical;
	LinearMemoryBlockManager *linear;
	PageManager *page;
};

// slab.c (linear memory)
SlabManager *createKernelSlabManager(void);

#endif
