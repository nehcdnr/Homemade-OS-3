#include"common.h"
#include"memory.h"
#include"page.h"
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

// global memory manager
static SlabManager *km = NULL;

void *allocate(size_t size){
	assert(km != NULL);
	return _allocateSlab(km, size);
}

void free(void *memory){
	assert(km != NULL);
	_releaseSlab(km, memory);
}
/*
size_t getPhysicalMemoryUsage(){

}

size_t getKernelMemoryUsage(){


}
*/
/*
void enablePaging(MemoryManager *m){
	setCR3((uint32_t)(m->pageDirectory));
	setCR0(getCR0() | 0x80000000);
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
			releaseBlock(m, (void*)blockAddress);
		}
	}
}

// kernel MemoryBlockManager
static MemoryBlockManager *initKernelPhysicalBlock(
	uintptr_t manageBase, uintptr_t manageBegin, uintptr_t manageEnd,
	uintptr_t minAddress, uintptr_t maxAddress
){
	MemoryBlockManager *pm = createMemoryBlockManager(manageBegin, manageEnd - manageBegin, minAddress, maxAddress);
	assert(((uintptr_t)pm) >= KERNEL_VIRTUAL_ADDRESS);
	assert(manageBase >= KERNEL_VIRTUAL_ADDRESS);
	const AddressRange extraAR[1] = {
		{manageBase - KERNEL_VIRTUAL_ADDRESS, manageEnd - manageBase, RESERVED, 0},
	};
	initUsableBlocks(pm, addressRange, addressRangeCount, extraAR, LENGTH_OF(extraAR));

	return pm;
}

static MemoryBlockManager *initKernelLinearManager(
	uintptr_t manageBase, uintptr_t manageBegin, uintptr_t manageEnd,
	uintptr_t minAddress, uintptr_t maxAddress
){
	MemoryBlockManager *lm = createMemoryBlockManager(manageBegin, manageEnd - manageBegin, minAddress, maxAddress);
	assert(((uintptr_t)lm) >= KERNEL_VIRTUAL_ADDRESS);
	assert(manageBase >= KERNEL_VIRTUAL_ADDRESS);
	const AddressRange extraAR[2] = {
		{manageBase, manageEnd - manageBase, RESERVED, 0},
		{manageEnd, OS_MAX_ADDRESS, USABLE, 0}
	};
	initUsableBlocks(lm, extraAR, LENGTH_OF(extraAR), extraAR, 0);
	return lm;
}

#ifdef NDEBUG
#define testMemoryManager() do()while(0)
#else
#define TEST_N (70)
static void testMemoryManager(void){
	uint8_t *p[TEST_N];
	int si[TEST_N];
	unsigned int r;
	int a, b, c;
	r=MIN_BLOCK_SIZE + 350;
	for(b=0;b<50;b++){
		for(a=0;a<TEST_N;a++){
			si[a]=r;
			p[a]=allocate(r);
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
					printk("%d %d\n",a2, p[a2][c]);
					panic("mem test checkpoint 2");
				}
			}
			free(p[a2]);
		}
	}
	printk("test ok\n");
	//kprintf("%x %x %x %x %x\n",a1,a2,a3, MIN_BLOCK_SIZE+(uintptr_t)a3,a4);
}
#endif

MemoryBlockManager *lm2;

void initKernelMemory(void){
	// find first usable memory address >= 1MB
	assert(km == NULL);
	uintptr_t manageEnd = KERNEL_VIRTUAL_ADDRESS + (16 << 20);
	uintptr_t manageBegin = KERNEL_VIRTUAL_ADDRESS + (1 << 20);
	MemoryBlockManager *pm = initKernelPhysicalBlock(
		KERNEL_VIRTUAL_ADDRESS, manageBegin, manageEnd,
		manageEnd - KERNEL_VIRTUAL_ADDRESS, findMaxAddress()
	);
	manageBegin = ((uintptr_t)pm) + getMetaSize(pm);
	MemoryBlockManager *lm = initKernelLinearManager(
		KERNEL_VIRTUAL_ADDRESS, manageBegin, manageEnd,
		KERNEL_VIRTUAL_ADDRESS, OS_MAX_ADDRESS
	);lm2 = lm;//TODO
	km = createSlabManager(pm);

	testMemoryManager();
}
