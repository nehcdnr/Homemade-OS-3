
typedef struct PageDirectory PageDirectory;
typedef struct PageTable PageTable;
PageDirectory *createPageDirectory(BlockManager *p);

enum PageType{
	KERNEL_PAGE,
	USER_PAGE
};
void map4KBKernelPage(BlockManager *m, PageDirectory *pd, uintptr_t linearAddress, uintptr_t physicalAddress);
