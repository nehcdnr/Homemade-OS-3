#include<std.h>

typedef struct MemoryBlockManager MemoryBlockManager;
typedef struct PageManager PageManager;
typedef struct MemoryBlockManager MemoryBlockManager;

#define PAGE_SIZE (4096)

PhysicalAddress translatePage(PageManager *p, uintptr_t linearAddress);

typedef struct LinearMemoryManager LinearMemoryManager;

#define USER_PAGE_FLAG (1 << 1)
#define WRITABLE_PAGE_FLAG (1 << 2)
enum PageType{
	KERNEL_PAGE = WRITABLE_PAGE_FLAG,
	USER_READ_ONLY_PAGE = USER_PAGE_FLAG,
	USER_WRITABLE_PAGE = USER_PAGE_FLAG + WRITABLE_PAGE_FLAG
};
// return 1 if success, 0 if error
int setPage(
	PageManager *p,	MemoryBlockManager *physical,
	uintptr_t linearAddress, PhysicalAddress physicalAddress,
	enum PageType pageType
);
void invalidatePage(
	PageManager *p,	MemoryBlockManager *physical,
	uintptr_t linearAddress
);

uint32_t getCR3(void);
