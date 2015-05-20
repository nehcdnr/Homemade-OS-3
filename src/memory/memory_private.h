#include<std.h>

// buddy.c (physical memory)
typedef struct MemoryBlockManager MemoryBlockManager;
// if failure, return NULL or UINTPTR_NULL
// if success, return address and *size = allocated size, which is >= input value
uintptr_t allocateBlock(MemoryBlockManager *m, size_t *size);
size_t getAllocatedBlockSize(MemoryBlockManager *m, uintptr_t address);
void releaseBlock(MemoryBlockManager *m, uintptr_t address);

uintptr_t getFirstBlockAddress(MemoryBlockManager *m);
MemoryBlockManager *createMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t endAddr
);
size_t getBlockManagerMetaSize(MemoryBlockManager *m);
int getBlockCount(MemoryBlockManager *m);
#define getTotalBlockSize(M) (getBlockCount(M) * MIN_BLOCK_SIZE)
int getFreeBlockSize(MemoryBlockManager *m);

// 4K~8M
// block is always aligned to MIN_BLOCK_SIZE
#define MIN_BLOCK_ORDER (12)
#define MIN_BLOCK_SIZE (1<<MIN_BLOCK_ORDER)
#define MAX_BLOCK_ORDER (23)
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
void _unmapPage_L(PageManager *p, MemoryBlockManager *physical, void *linearAddress, size_t size);

int _mapPage_LP(
	PageManager *p, MemoryBlockManager *physical,
	void *linearAddress, PhysicalAddress physicalAddress, size_t size,
	PageAttribute attribute
);
void _unmapPage_LP(PageManager *p, MemoryBlockManager *physical, void *linearAddress, size_t size);

int _mapExistingPages_L(
	MemoryBlockManager *physical, PageManager *dst, PageManager *src,
	void *dstLinear, uintptr_t srcLinear, size_t size,
	PageAttribute attribute
);

// linear + physical + page
typedef struct LinearMemoryManager{
	MemoryBlockManager *physical;
	MemoryBlockManager *linear;
	PageManager *page;
}LinearMemoryManager;

// slab.c (linear memory)
typedef struct SlabManager SlabManager;
void *allocateSlab(SlabManager *m, size_t size);
void releaseSlab(SlabManager *m, void *address);
SlabManager *createSlabManager(LinearMemoryManager *vm);
