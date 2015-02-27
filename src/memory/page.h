
typedef struct PageDirectory PageDirectory;
typedef struct PageTable PageTable;
PageDirectory *createPageDirectory(MemoryManager* m);

void set4KBKernelPage(MemoryManager *m, PageDirectory *pd, uintptr_t linearAddress, uintptr_t physicalAddress);
