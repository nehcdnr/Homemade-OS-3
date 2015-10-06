#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include<std.h>

// physical
typedef struct{
	uintptr_t value;
}PhysicalAddress;

typedef struct PhysicalMemoryBlockManager PhysicalMemoryBlockManager;

// if failure, return NULL, which is not a usable physical address
PhysicalAddress _allocatePhysicalPages(PhysicalMemoryBlockManager *physical, size_t size);
#define allocatePhysicalPages(SIZE) _allocatePhysicalPages(kernelLinear->physical, (SIZE))
void _releasePhysicalPages(PhysicalMemoryBlockManager *physical, PhysicalAddress address);
#define releasePhysicalPages(ADDRESS) _releasePhysicalPages(kernelLinear->physical, (ADDRESS))

// return free size
//size_t physicalMemoryUsage(size_t *totalSize);
//size_t getKernelMemoryUsage(size_t *totalSize);

// page
#define PAGE_SIZE (4096)
typedef struct PageManager PageManager;
extern PageManager *kernelPageManager;

extern const size_t sizeOfPageTableSet;
PageManager *createAndMapUserPageTable(uintptr_t reserveBase, uintptr_t reserveEnd, uintptr_t tablesLoadAddress);
void unmapUserPageTableSet(PageManager *p);

// must cli
void invalidatePageTable(PageManager *deletePage, PageManager *loadPage);
// must sti
void releaseInvalidatedPageTable(PageManager *deletePage);
// called when initialization failure
void releasePageTable(PageManager *deletePage);

#define USER_PAGE_FLAG (1 << 1)
#define WRITABLE_PAGE_FLAG (1 << 2)
#define NON_CACHED_PAGE_FLAG (1 << 4)
typedef enum PageAttribute{
	KERNEL_PAGE = WRITABLE_PAGE_FLAG,
	KERNEL_NON_CACHED_PAGE = WRITABLE_PAGE_FLAG + NON_CACHED_PAGE_FLAG,
	USER_READ_ONLY_PAGE = USER_PAGE_FLAG,
	USER_WRITABLE_PAGE = USER_PAGE_FLAG + WRITABLE_PAGE_FLAG
}PageAttribute;


uint32_t toCR3(PageManager *p);

int mapPage_L(PageManager *p, void *linearAddress, size_t size, PageAttribute attribute);
void unmapPage_L(PageManager *p, void *linearAddress, size_t size);

int mapPage_LP(PageManager *p, void *linearAddress, PhysicalAddress physicalAddress, size_t size, PageAttribute attribute);
void unmapPage_LP(PageManager *p, void *linearAddress, size_t size);

// kernel linear memory
void initKernelMemory(void);
typedef struct LinearMemoryManager LinearMemoryManager;
extern LinearMemoryManager *kernelLinear;

// kernel slab
// if failure, return NULL
void *allocateKernelMemory(size_t size);
void releaseKernelMemory(void *address);

// kernel/user page
// allocate new linear memory; map to specified physical address
void *mapPages(LinearMemoryManager *m, PhysicalAddress address, size_t size, PageAttribute attribute);
#define mapKernelPages(ADDRESS, SIZE, ATTRIBUTE) mapPages(kernelLinear, ADDRESS, SIZE, ATTRIBUTE)
// TODO: thread safe
void *mapExistingPages(
	LinearMemoryManager *dst, PageManager *src,
	uintptr_t srcLinear, size_t size, PageAttribute Attribute
);
#define mapExistingPagesToKernel(SRC, SRC_LINEAR, SIZE, ATTRIBUTE) \
	mapExistingPages(kernelLinear, SRC, SRC_LINEAR, SIZE, ATTRIBUTE)
// translate linear address to physical memory
PhysicalAddress checkAndTranslatePage(LinearMemoryManager *m, void *linearAddress);
// allocate new linear memory;
// map to the physical address translated from srcLinear by using the src page table
void *checkAndMapExistingPage(
	LinearMemoryManager *dst, LinearMemoryManager *src,
	void *srcLinear, size_t size, PageAttribute attribute
);

void unmapPages(LinearMemoryManager *m, void *linearAddress);
#define unmapKernelPages(ADDRESS) unmapPages(kernelLinear, ADDRESS)
int checkAndUnmapPages(LinearMemoryManager *m, void *linearAddress);

// allocate new linear memory and new physical memory
void *allocatePages(LinearMemoryManager *m, size_t size, PageAttribute attriute);
void *allocateKernelPages(size_t size, PageAttribute attribute);
//void releasePages(LinearMemoryManager *m, void *linearAddress);
//void releaseKernelPages(void *linearAddress);
int checkAndReleasePages(LinearMemoryManager *m, void *linearAddress);
int checkAndReleaseKernelPages(void *linearAddress);

#define NEW_ARRAY(V, L) (V) = (typeof(V))allocateKernelMemory((L) * sizeof(*(V)))
#define NEW(V) NEW_ARRAY(V, 1)
#define DELETE(V) releaseKernelMemory(V)

#ifndef NDEBUG
void testMemoryManager(void);
void testMemoryManager2(void);
void testMemoryManager3(void);
void testMemoryTask(void);
void testCreateThread(void);
#endif

//see kernel.ld
extern char KERNEL_LINEAR_BEGIN_SYMBOL;
extern char KERNEL_LINEAR_END_SYMBOL;
#define KERNEL_LINEAR_BEGIN ((uintptr_t)&KERNEL_LINEAR_BEGIN_SYMBOL)
#define KERNEL_LINEAR_END ((uintptr_t)&KERNEL_LINEAR_END_SYMBOL)

#define USER_LINEAR_BEGIN ((uintptr_t)0)
#define USER_LINEAR_END KERNEL_LINEAR_BEGIN

void *systemCall_allocateHeap(uintptr_t size, PageAttribute attribtue);
int systemCall_releaseHeap(void *address);
PhysicalAddress systemCall_translatePage(void *address);

typedef struct SlabManager SlabManager;
SlabManager *createUserSlabManager(void);
void *allocateSlab(SlabManager *m, size_t size);
void releaseSlab(SlabManager *m, void *address);

#endif
