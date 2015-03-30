
typedef struct PageDirectory PageDirectory;
typedef struct PageTable PageTable;
PageDirectory *createPageDirectory(void);

enum PageType{
	KERNEL_PAGE,
	USER_PAGE
};
void map4KBKernelPage(PageDirectory *pd, uintptr_t linearAddress, uintptr_t physicalAddress);
