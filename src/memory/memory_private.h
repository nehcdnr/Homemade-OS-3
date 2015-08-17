#include<std.h>

// buddy.c (physical memory)
typedef struct MemoryBlockManager MemoryBlockManager;
// if failure, return UINTPTR_NULL
// if success, return address and *size = allocated size, which is >= input value
typedef uint8_t MemoryBlockFlags;
uintptr_t allocateBlock(MemoryBlockManager *m, size_t *size, MemoryBlockFlags flags);
extern const size_t minBlockManagerSize;
extern const size_t maxBlockManagerSize;
size_t getAllocatedBlockSize(MemoryBlockManager *m, uintptr_t address);

// return whether an address is a valid argument of releaseBlock
int isReleasableAddress(MemoryBlockManager *m, uintptr_t address);
// no concern with flags
void releaseBlock(MemoryBlockManager *m, uintptr_t address);

uintptr_t getBeginAddress(MemoryBlockManager *m);

MemoryBlockManager *createMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t initEndAddr,
	uintptr_t maxEndAddr
);
int getMaxBlockCount(MemoryBlockManager *m);
size_t getMaxBlockManagerSize(MemoryBlockManager *m);
int getFreeBlockSize(MemoryBlockManager *m);

#define WITH_PHYSICAL_PAGES_FLAG ((MemoryBlockFlags)1)
uintptr_t allocateOrExtendLinearBlock(LinearMemoryManager *m, size_t *size, MemoryBlockFlags flags);
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
typedef struct PageManager PageManager;

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
typedef struct LinearMemoryManager{
	MemoryBlockManager *physical;
	MemoryBlockManager *linear;
	PageManager *page;
}LinearMemoryManager;

// slab.c (linear memory)
SlabManager *createKernelSlabManager(void);
