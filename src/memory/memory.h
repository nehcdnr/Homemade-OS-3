#include<std.h>

void initKernelMemory(void);

void *allocate(size_t size);
// block is always aligned to MIN_BLOCK_SIZE
void *allocateBlock(size_t size);
void free(void *memory);

#define freeBlock(M) free(M)

#define NEW_ARRAY(V, L) (V) = (typeof(V))allocate((L) * sizeof(*(V)))
#define NEW(V) NEW_ARRAY(V, 1)

#define INTERRUPT_ENTRY_MAX_SIZE (1<<20)
#define INTERRUPT_ENTRY_ADDRESS (0xc0000000)

// 4K~8M
#define MIN_BLOCK_ORDER (12)
#define MIN_BLOCK_SIZE (1<<MIN_BLOCK_ORDER)
#define MAX_BLOCK_ORDER (23)
#define MAX_BLOCK_SIZE (1<<MAX_BLOCK_ORDER)
