#include"common.h"
#include"memory.h"
#include"memory/page.h"
#include"assembly/assembly.h"
#include"multiprocessor/spinlock.h"
#include"memory_private.h"

// BIOS address range functions

enum AddressRangeType{
	USABLE = 1,
	RESERVED = 2,
	ACPI_RECLAIMABLE = 3,
	ACPI_NVS = 4,
	BAD_MEMORY = 5
};
static_assert(sizeof(enum AddressRangeType) == 4);

typedef struct AddressRange{
	uint64_t base;
	uint64_t size;
	enum AddressRangeType type;
	uint32_t extra;
}AddressRange;
static_assert(sizeof(AddressRange) == 24);

extern const AddressRange *const addressRange;
extern const int addressRangeCount;

#define OS_MAX_ADDRESS (((uintptr_t)0xffffffff) - ((uintptr_t)0xffffffff) % MIN_BLOCK_SIZE)
static_assert(OS_MAX_ADDRESS % MIN_BLOCK_SIZE == 0);

static uintptr_t findMaxAddress(void){
	int i;
	uint64_t maxAddr = 0;
	// kprintf("%d memory address ranges\n", addressRangeCount);
	for(i = 0; i < addressRangeCount; i++){
		const struct AddressRange *ar = addressRange + i;
		/*
		printk("type: %d base: %x %x size: %x %x\n", ar->type,
		(uint32_t)(ar->base >> 32), (uint32_t)(ar->base & 0xffffffff),
		(uint32_t)(ar->size >> 32), (uint32_t)(ar->size & 0xffffffff));
		 */
		if(ar->type == USABLE && ar->size != 0 &&
			maxAddr < ar->base + ar->size - 1){
			maxAddr = ar->base + ar->size - 1;
		}
	}
	if(maxAddr >= OS_MAX_ADDRESS){
		return OS_MAX_ADDRESS;
	}
	return maxAddr + 1;
}

// global memory manager
static SlabManager *kernelSlab = NULL;

void *allocateKernelMemory(size_t size){
	assert(kernelSlab != NULL);
	return allocateSlab(kernelSlab, size);
}

void releaseKernelMemory(void *linearAddress){
	assert(kernelSlab != NULL);
	releaseSlab(kernelSlab, linearAddress);
}

void *_mapKernelPage(LinearMemoryManager *m, PhysicalAddress physicalAddress, size_t size){
	assert(size % PAGE_SIZE == 0);
	assert(m->page != NULL && m->linear != NULL);
	// linear
	size_t l_size = size;
	void *linearAddress = (void*)allocateBlock(m->linear, &l_size);
	EXPECT(linearAddress != NULL);
	assert(((uintptr_t)linearAddress) % PAGE_SIZE == 0);
	// assume linear memory manager and page table are consistent
	// linearAddess[0 ~ l_size] are guaranteed to be available
	// it is safe to map pages of l_size
	int result = _mapPage_LP(m->page, m->physical, linearAddress, physicalAddress, l_size, KERNEL_PAGE);
	EXPECT(result == 1);

	return linearAddress;

	ON_ERROR;
	releaseBlock(m->linear, (uintptr_t)linearAddress);
	ON_ERROR;
	return NULL;
}

void *_mapKernelPagesFromExisting(
	LinearMemoryManager *dst, PageManager *src,
	uintptr_t srcLinear, size_t size
){
	size_t l_size = size;
	void *dstLinear = (void*)allocateBlock(dst->linear, &l_size);
	EXPECT(dstLinear != NULL);
	int result = _mapExistingPages_L(dst->physical, dst->page, src, dstLinear, srcLinear, l_size, KERNEL_PAGE);
	EXPECT(result != 0);

	return dstLinear;

	ON_ERROR;
	releaseBlock(dst->linear, (uintptr_t)dstLinear);
	ON_ERROR;
	return NULL;
}

void _unmapKernelPage(LinearMemoryManager *m, void *linearAddress){
	assert(m->page != NULL && m->linear != NULL);
	size_t s = getAllocatedBlockSize(m->linear, (uintptr_t)linearAddress);
	_unmapPage_LP(m->page, m->physical, linearAddress, s);
	releaseBlock(m->linear, (uintptr_t)linearAddress);
}

void *_allocateKernelPages(LinearMemoryManager *m, size_t size){
	// linear
	size_t l_size = size;
	void *linearAddress = (void*)allocateBlock(m->linear, &l_size);
	EXPECT(linearAddress != NULL);
	// physical
	int result = _mapPage_L(m->page, m->physical, linearAddress, l_size, KERNEL_PAGE);
	EXPECT(result == 1);

	return linearAddress;

	ON_ERROR;
	releaseBlock(m->linear, (uintptr_t)linearAddress);
	ON_ERROR;
	return NULL;
}

void _releaseKernelPages(LinearMemoryManager *m, void *linearAddress){
	size_t s = getAllocatedBlockSize(m->linear, (uintptr_t)linearAddress);
	_unmapPage_L(m->page, m->physical, linearAddress, s);
	releaseBlock(m->linear, (uintptr_t)linearAddress);
}

size_t physicalMemoryUsage(size_t *totalSize){
	size_t freeSize = getFreeBlockSize(kernelLinear->physical);
	if(totalSize != NULL){
		*totalSize = getTotalBlockSize(kernelLinear->physical);
	}
	return freeSize;
}
/*
size_t getKernelMemoryUsage(){
}
*/

// page

int mapPage_L(PageManager *p, void *linearAddress, size_t size, PageAttribute attribute){
	return _mapPage_L(p, kernelLinear->physical, linearAddress, size, attribute);
}
void unmapPage_L(PageManager *p, void *linearAddress, size_t size){
	_unmapPage_L(p, kernelLinear->physical, linearAddress, size);
}
int mapPage_LP(PageManager *p, void *linearAddress, PhysicalAddress physicalAddress, size_t size, PageAttribute attribute){
	return _mapPage_LP(p, kernelLinear->physical, linearAddress, physicalAddress, size, attribute);
}
void unmapPage_LP(PageManager *p, void *linearAddress, size_t size){
	return _unmapPage_LP(p, kernelLinear->physical, linearAddress, size);
}

// initialize kernel MemoryBlockManager
static void initUsableBlocks(MemoryBlockManager *m,
	const AddressRange *arArray1, int arLength1,
	const AddressRange *arArray2, int arLength2
){
	int b;
	const int bCount = getBlockCount(m);
	const uintptr_t firstBlockAddress = getFirstBlockAddress(m);
	for(b = 0; b < bCount; b++){
		uintptr_t blockAddress = firstBlockAddress + b * MIN_BLOCK_SIZE;
		assert(blockAddress + MIN_BLOCK_SIZE > blockAddress);
		int isInUnusable = 0, isInUsable = 0;
		int i;
		for(i = 0; isInUnusable == 0 && i < arLength1 + arLength2; i++){
			const AddressRange *ar = (i < arLength1? arArray1 + i: arArray2 + (i - arLength1));
			if( // completely covered by usable range
				ar->type == USABLE &&
				(ar->base <= blockAddress && ar->base + ar->size >= blockAddress + MIN_BLOCK_SIZE)
			){
				isInUsable = 1;
			}
			if( // partially covered by unusable range
				ar->type != USABLE &&
				!(ar->base >= blockAddress + MIN_BLOCK_SIZE || ar->base + ar->size <= blockAddress)
			){
				isInUnusable = 1;
			}
		}
		if(isInUnusable == 0 && isInUsable == 1){
			releaseBlock(m, blockAddress);
		}
	}
}

static MemoryBlockManager *initKernelPhysicalBlock(
	uintptr_t manageBase, uintptr_t manageBegin, uintptr_t manageEnd,
	uintptr_t minAddress, uintptr_t maxAddress
){
	MemoryBlockManager *m = createMemoryBlockManager(manageBegin, manageEnd - manageBegin, minAddress, maxAddress);
	const AddressRange extraAR[1] = {
		{manageBase - KERNEL_LINEAR_BEGIN, manageEnd - manageBase, RESERVED, 0}
	};
	initUsableBlocks(m, addressRange, addressRangeCount, extraAR, LENGTH_OF(extraAR));

	return m;
}

static MemoryBlockManager *initKernelLinearBlock(
	uintptr_t manageBase, uintptr_t manageBegin, uintptr_t manageEnd,
	uintptr_t minAddress, uintptr_t maxAddress
){
	MemoryBlockManager *m = createMemoryBlockManager(manageBegin, manageEnd - manageBegin, minAddress, maxAddress);
	const AddressRange extraAR[2] = {
		{manageBase, manageEnd - manageBase, RESERVED, 0},
		{minAddress, maxAddress, USABLE, 0}
	};
	initUsableBlocks(m, extraAR, 0, extraAR, LENGTH_OF(extraAR));

	return m;
}

#ifdef NDEBUG
#define testMemoryManager() do{}while(0)
#else
#define TEST_N (60)
static void testMemoryManager(void){
	uint8_t *p[TEST_N];
	int si[TEST_N];
	unsigned int r;
	int a, b, c;
	r=MIN_BLOCK_SIZE + 351;
	for(b=0;b<10;b++){
		for(a=0;a<TEST_N;a++){
			si[a]=r;
			p[a]=allocateKernelMemory(r);
			if(p[a] == NULL){
				// printk("a = %d, r = %d p[a] = %x\n", a, r, p[a]);
			}
			else{
				for(c=0;c<si[a]&&c<100;c++){
					p[a][c] =
					p[a][si[a]-c-1]= a+1;
				}
			}
			//r = 1 + (r*7 + 3) % (30 - 1);
			r = (r*79+3);
			if(r%5<3) r = r % 2048;
			else r = (r*17) % (MAX_BLOCK_SIZE - MIN_BLOCK_SIZE) + MIN_BLOCK_SIZE;
		}
		for(a=0;a<TEST_N;a++){
			int a2 = (a+r)%TEST_N;
			if(p[a2]==NULL)continue;
			for(c=0;c<si[a2]&&c<100;c++){
				if(p[a2][c] != a2+1 || p[a2][si[a2]-c-1] != a2+1){
					//printk("%x %x %d %d %d %d\n", p[a2], p[p[a2][c]-1],si[p[a2][c]-1], p[a2][c], p[a2][si[a2]-c-1], a2+1);
					panic("memory test failed");
				}
			}
			releaseKernelMemory((void*)p[a2]);
		}
	}
	//printk("test memory: ok\n");
	//printk("%x %x %x %x %x\n",a1,a2,a3, MIN_BLOCK_SIZE+(uintptr_t)a3,a4);
}
#endif

static LinearMemoryManager _kernelLinear;
LinearMemoryManager *kernelLinear = &_kernelLinear;
MemoryBlockManager *globalPhysical = NULL;

void initKernelMemory(void){
	assert(kernelLinear->linear == NULL && kernelLinear->page == NULL && kernelLinear->physical == NULL);
	// reserved... are linear address
	const uintptr_t reservedBase = KERNEL_LINEAR_BEGIN;
	uintptr_t reservedBegin = reservedBase + (1 << 20);
	uintptr_t reservedDirectMapEnd = reservedBase + (14 << 20);
	uintptr_t reservedEnd = reservedBase + (16 << 20);
	globalPhysical = kernelLinear->physical = initKernelPhysicalBlock(
		reservedBase, reservedBegin, reservedDirectMapEnd,
		0, findMaxAddress()
	);
	reservedBegin = ((uintptr_t)kernelLinear->physical) + getBlockManagerMetaSize(kernelLinear->physical);

	kernelLinear->linear = initKernelLinearBlock(
		reservedBase, reservedBegin, reservedEnd,
		KERNEL_LINEAR_BEGIN, KERNEL_LINEAR_END
	);
	reservedBegin = ((uintptr_t)kernelLinear->linear) + getBlockManagerMetaSize(kernelLinear->linear);
	kernelLinear->page = initKernelPageTable(
		reservedBase, reservedBegin, reservedEnd,
		KERNEL_LINEAR_BEGIN, KERNEL_LINEAR_END
	);

	kernelSlab = createSlabManager(kernelLinear);

	testMemoryManager();
}
