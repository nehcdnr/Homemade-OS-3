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
/*
static uintptr_t findFirstUsableMemory(const size_t manageSize){
	int i;
	uintptr_t manageBase = OS_MAX_ADDRESS;
	for(i = 0; i < addressRangeCount; i++){
		const AddressRange *ar = addressRange + i;
		if(
		ar->type == USABLE &&
		ar->base <= OS_MAX_ADDRESS - manageSize && // address < 4GB
		ar->base >= (1 << 20) && // address >= 1MB
		ar->size >= manageSize &&
		ar->base < manageBase
		){
			manageBase = (uintptr_t)ar->base;
		}
	}
	if(manageBase == OS_MAX_ADDRESS){
		panic("no memory for memory manager");
	}
	return manageBase;
}
*/

// memory manager collection

PhysicalAddress _allocatePhysicalPage(LinearMemoryManager *m, size_t size){
	size_t p_size = size;
	PhysicalAddress p_address = {allocateBlock(m->physical, &p_size)};
	return p_address;
}

void _releasePhysicalPage(LinearMemoryManager *m, PhysicalAddress address){
	releaseBlock(m->physical, address.value);
}

void *_mapPage(LinearMemoryManager *m, PhysicalAddress p_address, size_t size){
	// linear
	size_t l_size = size;
	uintptr_t l_address = allocateBlock(m->linear, &l_size);
	if(l_address == UINTPTR_NULL){
		goto error_linear;
	}
	assert((l_address) % PAGE_SIZE == 0);
	// page
	size_t s = 0;
	while(s < l_size){
		PhysicalAddress p_address_s = {p_address.value + s};
		if(setKernelPage(m, (l_address) + s, p_address_s, KERNEL_PAGE) == 0){
			goto error_page;
		}
		s += PAGE_SIZE;
	}
	// succeeded
	return (void*)l_address;
	// undo
	error_page: // [l_address ~ l_address+s-PAGE_SIZE] are mapped; [l_address + s] is not mapped
	while(s != 0){
		s -= PAGE_SIZE;
		invalidatePage(m, ((uintptr_t)l_address) + s);
	}
	releaseBlock(m->linear, (uintptr_t)l_address);
	error_linear:
	return NULL;
}

void _unmapPage(LinearMemoryManager *m, void *l_address){
	size_t s = getAllocatedBlockSize(m->linear, (uintptr_t)l_address);
	while(s != 0){
		s -= PAGE_SIZE;
		invalidatePage(m, ((uintptr_t)l_address) + s);
	}
	releaseBlock(m->linear, (uintptr_t)l_address);
}

void *_allocateAndMapPage(LinearMemoryManager *m, size_t size){
	// physical
	PhysicalAddress p_address = _allocatePhysicalPage(m, size);
	if(p_address.value == UINTPTR_NULL){
		goto error_physical;
	}
	// linear
	void *l_address = _mapPage(m, p_address, size);
	if(l_address == NULL){
		goto error_map;
	}
	// succeeded
	return l_address;
	// undo
	error_map:
	releaseBlock(m->physical, p_address.value);
	error_physical:
	return NULL;
}

void _unmapAndReleasePage(LinearMemoryManager *m, void* l_address){
	PhysicalAddress p_address = translatePage(m->page, l_address);
	_unmapPage(m, l_address);
	_releasePhysicalPage(m, p_address);
}

// global memory manager
static LinearMemoryManager kernelMemory;
static SlabManager *kernelSlab = NULL;

void *allocateKernelMemory(size_t size){
	assert(kernelSlab != NULL);
	return allocateSlab(kernelSlab, size);
}

void releaseKernelMemory(void *address){
	assert(kernelSlab != NULL);
	releaseSlab(kernelSlab, address);
}

PhysicalAddress allocatePhysicalPage(size_t size){
	return _allocatePhysicalPage(&kernelMemory, size);
}
void releasePhysicalPage(PhysicalAddress address){
	_releasePhysicalPage(&kernelMemory, address);
}

void *mapPageToPhysical(PhysicalAddress address, size_t size){
	assert(size % PAGE_SIZE == 0);
	assert(kernelMemory.page != NULL && kernelMemory.linear != NULL);
	return _mapPage(&kernelMemory, address, size);
}

void unmapPageToPhysical(void *linearAddress){
	assert(kernelMemory.page != NULL && kernelMemory.linear != NULL);
	_unmapPage(&kernelMemory, linearAddress);
}
void *allocateAndMapPage(size_t size){
	return _allocateAndMapPage(&kernelMemory, size);
}
void unmapAndReleasePage(void *linearAddress){
	_unmapAndReleasePage(&kernelMemory, linearAddress);
}

/*
size_t getPhysicalMemoryUsage(){

}

size_t getKernelMemoryUsage(){

}
*/
/*
UserPageTable *mapUserPageTable(PhysicalAddress p){
	return _mapUserPageTable(&kernelMemory, kernelSlab, p);
}
PhysicalAddress unmapUserPageTable(UserPageTable *p){
	return _unmapUserPageTable(&kernelMemory, kernelSlab, p);
}
UserPageTable *createUserPageTable(void){
	return _createUserPageTable(&kernelMemory, kernelSlab);
}
void deleteUserPageTable(UserPageTable *p){
	_deleteUserPageTable(&kernelMemory, kernelSlab, p);
}
*/
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

// kernel MemoryBlockManager
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
static void testMemoryManager(void){return;
	uint8_t *p[TEST_N];
	int si[TEST_N];
	unsigned int r;
	int a, b, c;
	r=MIN_BLOCK_SIZE + 351;
	for(b=0;b<40;b++){
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

void initKernelMemory(void){
	assert(kernelMemory.linear == NULL && kernelMemory.page == NULL && kernelMemory.physical == NULL);
	// reserved... are linear address
	const uintptr_t reservedBase = KERNEL_LINEAR_BEGIN;
	uintptr_t reservedBegin = reservedBase + (1 << 20);
	uintptr_t reservedDirectMapEnd = reservedBase + (14 << 20);
	uintptr_t reservedEnd = reservedBase + (16 << 20);
	kernelMemory.physical = initKernelPhysicalBlock(
		reservedBase, reservedBegin, reservedDirectMapEnd,
		0, findMaxAddress()
	);
	reservedBegin = ((uintptr_t)kernelMemory.physical) + getBlockManagerMetaSize(kernelMemory.physical);

	kernelMemory.linear = initKernelLinearBlock(
		reservedBase, reservedBegin, reservedEnd,
		KERNEL_LINEAR_BEGIN, KERNEL_LINEAR_END
	);
	reservedBegin = ((uintptr_t)kernelMemory.linear) + getBlockManagerMetaSize(kernelMemory.linear);
	kernelMemory.page = initKernelPageTable(
		reservedBase, reservedBegin, reservedEnd,
		KERNEL_LINEAR_BEGIN, KERNEL_LINEAR_END
	);

	kernelSlab = createSlabManager(&kernelMemory);

	testMemoryManager();
}
