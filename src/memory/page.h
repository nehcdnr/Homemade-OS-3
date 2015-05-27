#include<std.h>

typedef struct MemoryBlockManager MemoryBlockManager;
typedef struct PageManager PageManager;
typedef struct LinearMemoryManager LinearMemoryManager;

//#define PAGE_SIZE (4096)

uint32_t getCR3(void);
