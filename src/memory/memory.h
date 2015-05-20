#include<std.h>

// physical
typedef struct{
	uintptr_t value;
}PhysicalAddress;

typedef struct MemoryBlockManager MemoryBlockManager;
extern MemoryBlockManager *globalPhysical;

PhysicalAddress _allocatePhysicalPages(MemoryBlockManager *physical, size_t size);
#define allocatePhysicalPages(SIZE) _allocatePhysicalPages(globalPhysical, (SIZE))
void _releasePhysicalPages(MemoryBlockManager *physical, PhysicalAddress address);
#define releasePhysicalPages(ADDRESS) _releasePhysicalPages(globalPhysical, (ADDRESS))

// return free size
size_t physicalMemoryUsage(size_t *totalSize);
//size_t getKernelMemoryUsage(size_t *totalSize);

// page
typedef struct PageManager PageManager;
extern PageManager *kernelPageManager;

extern const size_t sizeOfPageTableSet;
PageManager *createAndMapUserPageTable(uintptr_t targetAddress);
void unmapUserPageTableSet(PageManager *p);
void deleteUserPageTable(PageManager *p);

#define USER_PAGE_FLAG (1 << 1)
#define WRITABLE_PAGE_FLAG (1 << 2)
typedef enum{
	KERNEL_PAGE = WRITABLE_PAGE_FLAG,
	USER_READ_ONLY_PAGE = USER_PAGE_FLAG,
	USER_WRITABLE_PAGE = USER_PAGE_FLAG + WRITABLE_PAGE_FLAG
}PageAttribute;


uint32_t toCR3(PageManager *p);

int mapPage_L(PageManager *p, void *linearAddress, size_t size, PageAttribute attribute);
void unmapPage_L(PageManager *p, void *linearAddress, size_t size);

int mapPage_LP(PageManager *p, void *linearAddress, PhysicalAddress physicalAddress, size_t size, PageAttribute attribute);
void unmapPage_LP(PageManager *p, void *linearAddress, size_t size);
/*
int mapExistingPages_L(
	PageManager *dst, PageManager *src,
	uintptr_t dstLinear, uintptr_t srcLinear, size_t size,
	PageAttribute attribute
);
*/
// kernel linear memory
void initKernelMemory(void);
typedef struct LinearMemoryManager LinearMemoryManager;
extern LinearMemoryManager *kernelLinear;

void *allocateKernelMemory(size_t size);
void releaseKernelMemory(void *address);

void *_mapKernelPage(LinearMemoryManager *m, PhysicalAddress address, size_t size);
#define mapKernelPage(ADDRESS, SIZE) _mapKernelPage(kernelLinear, ADDRESS, SIZE)
void *_mapKernelPagesFromExisting(
	LinearMemoryManager *dst, PageManager *src,
	uintptr_t srcLinear, size_t size
);
#define mapKernelPagesFromExisting(SRC, SRC_LINEAR, SIZE) \
_mapKernelPagesFromExisting(kernelLinear, SRC, SRC_LINEAR, SIZE)
void _unmapKernelPage(LinearMemoryManager *m, void *linearAddress);
#define unmapKernelPage(ADDRESS) _unmapKernelPage(kernelLinear, ADDRESS)

void *_allocateKernelPages(LinearMemoryManager *m, size_t size);
#define allocateKernelPages(SIZE) _allocateKernelPages(kernelLinear, SIZE)
void _releaseKernelPages(LinearMemoryManager *m, void *linearAddress);
#define releaseKernelPages(ADDRESS) _releaseKernelPages(kernelLinear, ADDRESS)

#define NEW_ARRAY(V, L) (V) = (typeof(V))allocateKernelMemory((L) * sizeof(*(V)))
#define NEW(V) NEW_ARRAY(V, 1)
#define DELETE(V) releaseKernelMemory(V)

//see kernel.ld
extern char KERNEL_LINEAR_BEGIN_SYMBOL;
extern char KERNEL_LINEAR_END_SYMBOL;
#define KERNEL_LINEAR_BEGIN ((uintptr_t)&KERNEL_LINEAR_BEGIN_SYMBOL)
#define KERNEL_LINEAR_END ((uintptr_t)&KERNEL_LINEAR_END_SYMBOL)

#define USER_LINEAR_BEGIN ((uintptr_t)0)
#define USER_LINEAR_END KERNEL_LINEAR_BEGIN
