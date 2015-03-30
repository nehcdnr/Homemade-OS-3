#include<std.h>

void initKernelMemory(void);

size_t getAllocatedSize(void);
void *allocateAligned(uintptr_t size, size_t align);
void *allocateFixed(size_t size);
#define NEW_ALIGNED(V, A) (V) = (typeof(V))allocateAligned(sizeof(*(V)), (A))
#define NEW_ARRAY(V, L) (V) = (typeof(V))allocateFixed((L) * sizeof(*(V)))
#define NEW(V) NEW_ARRAY(V, 1)


// 4K~8M
#define MIN_BLOCK_ORDER (12)
#define MIN_BLOCK_SIZE (1<<MIN_BLOCK_ORDER)
#define MAX_BLOCK_ORDER (23)
#define MAX_BLOCK_SIZE (1<<MAX_BLOCK_ORDER)
// NULL if not found
void *allocateBlock(size_t size);
void freeBlock(void *address);
