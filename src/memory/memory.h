
typedef struct MemoryManager MemoryManager;

MemoryManager *createMemoryManager(unsigned base, unsigned size);
MemoryManager *initKernelMemoryManager(void);

void *allocateAligned(MemoryManager *m, unsigned size, unsigned align);
void* allocate(MemoryManager* m, unsigned size);
unsigned getAllocatedSize(MemoryManager* m);

typedef struct PageManager PageManager;
PageManager *initKernelPageManager(MemoryManager *m);
unsigned getUsingSize(PageManager *p);
unsigned getUsableSize(PageManager *p);
