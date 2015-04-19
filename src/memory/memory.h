#include<std.h>

void initKernelMemory(void);

void *allocate(size_t size);

void free(void *memory);

size_t getPhysicalMemoryUsage(void);
size_t getKernelMemoryUsage(void);

#define NEW_ARRAY(V, L) (V) = (typeof(V))allocate((L) * sizeof(*(V)))
#define NEW(V) NEW_ARRAY(V, 1)

extern int KERNEL_VIRTUAL_ADDRESS_SYMBOL;
#define KERNEL_VIRTUAL_ADDRESS ((uintptr_t)&KERNEL_VIRTUAL_ADDRESS_SYMBOL)

#define INTERRUPT_ENTRY_MAX_SIZE (1<<20)
#define INTERRUPT_ENTRY_ADDRESS (0xc0000000)

// 4K~8M
// block is always aligned to MIN_BLOCK_SIZE
#define MIN_BLOCK_ORDER (12)
#define MIN_BLOCK_SIZE (1<<MIN_BLOCK_ORDER)
#define MAX_BLOCK_ORDER (23)
#define MAX_BLOCK_SIZE (1<<MAX_BLOCK_ORDER)
