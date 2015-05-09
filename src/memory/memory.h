#include<std.h>

typedef struct{
	uintptr_t value;
}PhysicalAddress;

void initKernelMemory(void);

void *allocateKernelMemory(size_t size);
void releaseKernelMemory(void *address);

PhysicalAddress allocatePhysicalPage(size_t size);
void releasePhysicalPage(PhysicalAddress address);

void *mapPage(PhysicalAddress address, size_t size);
void unmapPage(void *linearAddress);

void *allocateAndMapPage(size_t size);
void unmapAndReleasePage(void *linearAddress);

size_t getPhysicalMemoryUsage(void);
size_t getKernelMemoryUsage(void);

typedef struct PageManager PageManager;
//UserPageTable *mapUserPageTable(PhysicalAddress p);
//PhysicalAddress unmapUserPageTable(UserPageTable *p);
extern const size_t sizeOfPageTableSet;
PageManager *createUserPageTable(uintptr_t reservedBase, uintptr_t reservedEnd, void *pageTableSetAddress);
void deleteUserPageTable(PageManager *p);


#define NEW_ARRAY(V, L) (V) = (typeof(V))allocateKernelMemory((L) * sizeof(*(V)))
#define NEW(V) NEW_ARRAY(V, 1)
#define DELETE(V) releaseKernelMemory(V)

//see kernel.ld
extern char KERNEL_LINEAR_BASE_SYMBOL;
extern char KERNEL_LINEAR_END_SYMBOL;
#define KERNEL_LINEAR_BASE ((uintptr_t)&KERNEL_LINEAR_BASE_SYMBOL)
#define KERNEL_LINEAR_END ((uintptr_t)&KERNEL_LINEAR_END_SYMBOL)

#define USER_LINEAR_END KERNEL_LINEAR_BASE
#define USER_LINEAR_BEGIN ((uintptr_t)0)

// 4K~8M
// block is always aligned to MIN_BLOCK_SIZE
#define MIN_BLOCK_ORDER (12)
#define MIN_BLOCK_SIZE (1<<MIN_BLOCK_ORDER)
#define MAX_BLOCK_ORDER (23)
#define MAX_BLOCK_SIZE (1<<MAX_BLOCK_ORDER)
