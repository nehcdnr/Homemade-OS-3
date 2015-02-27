#include<std.h>

typedef struct MemoryManager MemoryManager;

MemoryManager *createMemoryManager(uintptr_t base, size_t size);
MemoryManager *initKernelMemoryManager(void);

void *allocateAligned(MemoryManager *m, uintptr_t size, size_t align);
void* allocate(MemoryManager* m, size_t size);
size_t getAllocatedSize(MemoryManager* m);

typedef struct PageManager PageManager;
PageManager *initKernelPageManager(MemoryManager *m);
size_t getUsingSize(PageManager *p);
size_t getUsableSize(PageManager *p);
