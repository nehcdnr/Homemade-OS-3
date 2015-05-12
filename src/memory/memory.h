#include<std.h>

typedef struct{
	uintptr_t value;
}PhysicalAddress;

void initKernelMemory(void);

void *allocateKernelMemory(size_t size);
void releaseKernelMemory(void *address);

PhysicalAddress allocatePhysicalPage(size_t size);
void releasePhysicalPage(PhysicalAddress address);

void *mapPageToPhysical(PhysicalAddress address, size_t size);
void unmapPageToPhysical(void *linearAddress);

void *allocateAndMapPage(size_t size);
void unmapAndReleasePage(void *linearAddress);

size_t getPhysicalMemoryUsage(void);
size_t getKernelMemoryUsage(void);


typedef struct PageManager PageManager;
//UserPageTable *mapUserPageTable(PhysicalAddress p);
//PhysicalAddress unmapUserPageTable(UserPageTable *p);
extern const size_t sizeOfPageTableSet;
PageManager *createAndMapUserPageTable(uintptr_t targetAddress);
void unmapUserPageTableSet(PageManager *p);
uint32_t toCR3(PageManager *p);
void deleteUserPageTable(PageManager *p);

void mapPageFormLinear(PageManager *p, void *linearAddress, size_t size);
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
