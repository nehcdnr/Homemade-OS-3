#include<std.h>

// buddy.c (physical memory)
typedef struct MemoryBlockManager MemoryBlockManager;
// if failure, return NULL
// if success, return address and *size = allocated size, which is >= input value
void *allocateBlock(MemoryBlockManager *m, size_t *size);
size_t getAllocatedBlockSize(MemoryBlockManager *m, void *address);
void releaseBlock(MemoryBlockManager *m, void *address);

uintptr_t getFirstBlockAddress(MemoryBlockManager *m);
MemoryBlockManager *createMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t endAddr
);
size_t getMetaSize(MemoryBlockManager *m);
int getBlockCount(MemoryBlockManager *m);

// page.c
typedef struct TopLevelPageTable TopLevelPageTable;
TopLevelPageTable *initKernelPageTable(uintptr_t manageBase, uintptr_t manageBegin, uintptr_t manageEnd);

// linear, physical, and page
typedef struct{
	MemoryBlockManager *physical;
	MemoryBlockManager *linear;
	TopLevelPageTable *page;
}LinearMemoryManager;
void *allocateAndMapPhysical(LinearMemoryManager *m, size_t size);
void unmapAndReleasePhysical(LinearMemoryManager *m, void* address);
void *mapPhysical(LinearMemoryManager *m, void *address, size_t size);
void unmapPhysical(LinearMemoryManager *m, void *address);

// slab.c (linear memory)
typedef struct SlabManager SlabManager;
void *allocateSlab(SlabManager *m, size_t size);
void releaseSlab(SlabManager *m, void *address);
SlabManager *createSlabManager(LinearMemoryManager *vm);
