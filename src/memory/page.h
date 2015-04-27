typedef struct MemoryBlockManager MemoryBlockManager;
typedef struct TopLevelPageTable TopLevelPageTable;

TopLevelPageTable *createPageTable(MemoryBlockManager *m);
void deletePageDirectory(MemoryBlockManager *m, TopLevelPageTable *pd);

#define PAGE_SIZE (4096)

enum PageType{
	KERNEL_PAGE,
	USER_PAGE
};

void *translatePage(TopLevelPageTable *p, void *linearAddress);
// return 1 if success, 0 if error
int mapKernelPage(TopLevelPageTable *pd, uintptr_t linearAddress, uintptr_t physicalAddress);
void unmapPage(TopLevelPageTable *pd, uintptr_t linearAddress);
struct PageDirectory;
void setCR3(struct PageDirectory *t);
struct PageDirectory *getCR3(void);
