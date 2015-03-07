#include<std.h>

typedef struct MemoryManager MemoryManager;

MemoryManager *createMemoryManager(uintptr_t base, size_t size);
MemoryManager *initKernelMemoryManager(void);

void *allocateAligned(MemoryManager *m, uintptr_t size, size_t align);
void *allocateFixedSize(MemoryManager* m, size_t size);
#define NEW_ALIGNED(V, M, A) (V) = (typeof(V))allocateAligned((M), sizeof(*(V)), (A))
#define NEW_ARRAY(V, M, L) (V) = (typeof(V))allocateFixedSize((M), (L) * sizeof(*(V)))
#define NEW(V, M) NEW_ARRAY(V, M, 1)

size_t getAllocatedSize(MemoryManager* m);

// 4K~16M
#define MIN_BLOCK_ORDER (12)
#define MIN_BLOCK_SIZE (1<<MIN_BLOCK_ORDER)
#define MAX_BLOCK_ORDER (23)
#define MAX_BLOCK_SIZE (1<<MAX_BLOCK_ORDER)
typedef struct BlockManager BlockManager;
BlockManager *initKernelBlockManager(MemoryManager *m);
// NULL if not found
void *allocateBlock(BlockManager *m, size_t size);
void freeBlock(BlockManager *m, void *address);
