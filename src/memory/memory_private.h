#include<std.h>

// buddy.c (physical memory)
typedef struct MemoryBlockManager MemoryBlockManager;
void *allocateBlock(MemoryBlockManager *m, size_t size);
void releaseBlock(MemoryBlockManager *m, void *memory);
uintptr_t getFirstBlockAddress(MemoryBlockManager *m);
MemoryBlockManager *createMemoryBlockManager(
	uintptr_t manageBase,
	size_t manageSize,
	uintptr_t beginAddr,
	uintptr_t endAddr
);
size_t getMetaSize(MemoryBlockManager *m);
int getBlockCount(MemoryBlockManager *m);

// slab.c (linear memory)
typedef struct SlabManager SlabManager;
void *_allocateSlab(SlabManager *m, size_t size);
void _releaseSlab(SlabManager *m, void *address);
SlabManager *createSlabManager(MemoryBlockManager *bm);
