typedef struct MemoryBlockManager MemoryBlockManager;
typedef struct PageManager PageManager;

#define PAGE_SIZE (4096)

PhysicalAddress translatePage(PageManager *p, void *linearAddress);

typedef struct LinearMemoryManager LinearMemoryManager;
// return 1 if success, 0 if error
int setKernelPage(
	LinearMemoryManager *m,
	uintptr_t linearAddress, PhysicalAddress physicalAddress
);
void invalidatePage(
	LinearMemoryManager *m,
	uintptr_t linearAddress
);

void setCR3(uint32_t value);
uint32_t getCR3(void);
