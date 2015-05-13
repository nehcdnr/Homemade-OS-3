#include<std.h>

typedef struct{
	uintptr_t value;
}PhysicalAddress;

void initKernelMemory(void);

// kernel linear memory
void *allocateKernelMemory(size_t size);
void releaseKernelMemory(void *address);

void *mapPageToPhysical(PhysicalAddress address, size_t size);
void unmapPageToPhysical(void *linearAddress);

void *allocateAndMapPages(size_t size);
void unmapAndReleasePages(void *linearAddress);

// physical
PhysicalAddress allocatePhysicalPages(size_t size);
void releasePhysicalPages(PhysicalAddress address);

size_t getPhysicalMemoryUsage(void);
size_t getKernelMemoryUsage(void);

typedef struct PageManager PageManager;
extern PageManager *kernelPageManager;

extern const size_t sizeOfPageTableSet;
PageManager *createAndMapUserPageTable(uintptr_t targetAddress);
void unmapUserPageTableSet(PageManager *p);

int mapExistingPages(
	PageManager *dst, PageManager *src,
	uintptr_t dstLinear, uintptr_t srcLinear, size_t size
);
int mapNewPhysicalPages(
	PageManager *p,
	void *linear, size_t size
);

uint32_t toCR3(PageManager *p);
void deleteUserPageTable(PageManager *p);

int mapPageFromLinear(PageManager *p, void *linearAddress, size_t size);
void unmapPageFromLinear(PageManager *p, void *linearAddress, size_t size);

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
