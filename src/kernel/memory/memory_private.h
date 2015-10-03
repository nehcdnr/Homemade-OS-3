#ifndef MEMORY_PRIVATE_H_INCLUDED
#define MEMORY_PRIVATE_H_INCLUDED

#include<std.h>
#include"memory.h"

// buddy.c (physical memory)
typedef struct MemoryBlockManager MemoryBlockManager;
// if failure, return UINTPTR_NULL
// if success, return address and *size = allocated size, which is >= input value
typedef uint8_t MemoryBlockFlags;
uintptr_t allocateBlock(MemoryBlockManager *m, size_t *size, MemoryBlockFlags flags);
//extern const size_t minBlockManagerSize;
//extern const size_t maxBlockManagerSize;
extern const size_t minLinearBlockManagerSize;
extern const size_t maxLinearBlockManagerSize;

// return whether an address is a valid argument of releaseBlock
int isReleasableAddress(MemoryBlockManager *m, uintptr_t address);
// no concern with flags
void releaseBlock(MemoryBlockManager *m, uintptr_t address);

uintptr_t getBeginAddress(MemoryBlockManager *m);

MemoryBlockManager *createMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr
);

typedef struct PhysicalMemoryBlockManager PhysicalMemoryBlockManager;

PhysicalMemoryBlockManager *createPhysicalMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr
);

//TODO: change name
size_t getBlockManagerSize2(PhysicalMemoryBlockManager *m);
// change reference count from 0 to 1
uintptr_t allocatePhysicalBlock(PhysicalMemoryBlockManager *m, size_t *size);
// if referenceCount == MAX_REFERENCE_COUNT, do not increase it and return 0
// otherwise, increase and return 1
int addPhysicalBlockReference(PhysicalMemoryBlockManager *m, uintptr_t address);
// subtract referenceCount and return the new value
// if the new value == 0, release the block
void releaseOrUnmapPhysicalBlock(PhysicalMemoryBlockManager *m, uintptr_t address);

typedef struct LinearMemoryBlockManager LinearMemoryBlockManager;

LinearMemoryBlockManager *createLinearBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr,
	uintptr_t maxEndAddr
);

size_t getBlockManagerSize(MemoryBlockManager *m);
size_t getMaxBlockManagerSize(LinearMemoryBlockManager *m);
int getBlockCount(MemoryBlockManager *m);
int getMaxBlockCount(LinearMemoryBlockManager *m);
size_t getFreeBlockSize(MemoryBlockManager *m);
size_t getFreeLinearBlockSize(LinearMemoryBlockManager *m);
uintptr_t getLinearBeginAddress(LinearMemoryBlockManager *m);

size_t getAllocatedBlockSize(LinearMemoryBlockManager *m, uintptr_t address);
// release linear blocks only
void releaseLinearBlock(LinearMemoryBlockManager *m, uintptr_t address);

#define WITH_PHYSICAL_PAGES_FLAG ((MemoryBlockFlags)1)

// allocate linear blocks only
uintptr_t allocateOrExtendLinearBlock(LinearMemoryManager *m, size_t *size, MemoryBlockFlags flags);
// release linear blocks, pages, and physical blocks
int _checkAndUnmapLinearBlock(LinearMemoryManager *m, uintptr_t linearAddress, int releasePhysical);
#define checkAndUnmapLinearBlock(M, A) _checkAndUnmapLinearBlock(M, A, 0)
#define checkAndReleaseLinearBlock(M, A) _checkAndUnmapLinearBlock(M, A, 1)
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

int _mapPage_L(
	PageManager *p, MemoryBlockManager *physical,
	void *linearAddress, size_t size,
	PageAttribute attribute
);
void _unmapPage(PageManager *p, MemoryBlockManager *physical, void *linearAddress, size_t size, int releasePhysical);
#define _unmapPage_L(PAGE, PHYSICAL, ADDRESS, SIZE) _unmapPage(PAGE, PHYSICAL, ADDRESS, SIZE, 1)

int _mapPage_LP(
	PageManager *p, MemoryBlockManager *physical,
	void *linearAddress, PhysicalAddress physicalAddress, size_t size,
	PageAttribute attribute
);
#define _unmapPage_LP(PAGE, PHYSICAL, ADDRESS, SIZE) _unmapPage(PAGE, PHYSICAL, ADDRESS, SIZE, 0)

int _mapExistingPages_L(
	MemoryBlockManager *physical, PageManager *dst, PageManager *src,
	void *dstLinear, uintptr_t srcLinear, size_t size,
	PageAttribute attribute
);

PhysicalAddress translateExistingPage(PageManager *p, void *linearAddress);

// linear + physical + page
struct LinearMemoryManager{
	MemoryBlockManager *physical;
	LinearMemoryBlockManager *linear;
	PageManager *page;
};

// slab.c (linear memory)
SlabManager *createKernelSlabManager(void);

#endif
