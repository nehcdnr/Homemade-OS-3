#include<std.h>

// buddy.c (physical memory)
typedef struct MemoryBlockManager MemoryBlockManager;
// if failure, return NULL
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

// page.c
typedef struct PageManager PageManager;
PageManager *initKernelPageTable(
	uintptr_t manageBase,
	uintptr_t manageBegin,
	uintptr_t manageEnd,
	uintptr_t kernelLinearBase,
	uintptr_t kernelLinearEnd
);

// linear + physical + page
typedef struct LinearMemoryManager{
	MemoryBlockManager *physical;
	MemoryBlockManager *linear;
	PageManager *page;
}LinearMemoryManager;

PhysicalAddress _allocatePhysicalPage(LinearMemoryManager *m, size_t size);
void _releasePhysicalPage(LinearMemoryManager *m, PhysicalAddress address);
void *_mapPage(LinearMemoryManager *m, PhysicalAddress address, size_t size);
void _unmapPage(LinearMemoryManager *m, void *address);
void *_allocateAndMapPage(LinearMemoryManager *m, size_t size);
void _unmapAndReleasePage(LinearMemoryManager *m, void* address);

// slab.c (linear memory)
typedef struct SlabManager SlabManager;
void *allocateSlab(SlabManager *m, size_t size);
void releaseSlab(SlabManager *m, void *address);
SlabManager *createSlabManager(LinearMemoryManager *vm);
