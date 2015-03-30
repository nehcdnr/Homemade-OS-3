#include"common.h"
#include"memory.h"
#include"page.h"
#include"assembly/assembly.h"
#include"multiprocessor/spinlock.h"

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

// MemoryManager functions

#define PADDING_SIZE(VALUE, ALIGN) ((VALUE) % (ALIGN) == 0? 0: (ALIGN) - (VALUE) % (ALIGN))

typedef struct MemoryManager{
	uintptr_t base;
	size_t size;
	size_t usedSize;
	Spinlock lock;
}MemoryManager;

static MemoryManager *createMemoryManager(uintptr_t base, size_t size){
	MemoryManager *m = (MemoryManager*)base;
	m->base = base;
	m->size = size;
	m->usedSize = sizeof(MemoryManager);
	m->lock = initialSpinlock;
	return m;
}

static void *_allocateAligned(MemoryManager *m, size_t size, size_t align){
	const size_t padSize = PADDING_SIZE(m->base + m->usedSize, align);
	void *address = NULL;
	acquireLock(&m->lock);
	if(size <= m->size - padSize - m->usedSize){
		address = (void*)(m->base + m->usedSize + padSize);
		m->usedSize += size + padSize;
	}
	releaseLock(&m->lock);
	assert(address != NULL);
	return address;
}

#define DEFAULT_ALIGN ((size_t)4)
static void *_allocateFixed(MemoryManager *m, size_t size){
	return _allocateAligned(m, size, DEFAULT_ALIGN);
}

// BlockManager functions

typedef struct MemoryBlock{
	unsigned sizeOrder;
	struct MemoryBlock**prev, *next;
}MemoryBlock;

#define PAGE_UNIT_SIZE MIN_BLOCK_SIZE
typedef struct MemoryBlockManager{
	int blockCount;
	size_t freeSize;
	Spinlock lock;
	MemoryBlock *freeBlock[MAX_BLOCK_ORDER - MIN_BLOCK_ORDER + 1];
	MemoryBlock *block;
}MemoryBlockManager;

static void initMemoryBlock(MemoryBlock *mb){
	mb->sizeOrder = MIN_BLOCK_ORDER;
	mb->next = NULL;
	mb->prev = NULL;
}

static uintptr_t getAddress(MemoryBlockManager *m, MemoryBlock *mb){
	return ((uintptr_t)(mb - (m->block))) << MIN_BLOCK_ORDER;
}

static MemoryBlock *getBlock(MemoryBlockManager *m, uintptr_t address){
	assert(address % MIN_BLOCK_SIZE == 0);
	int i = address / MIN_BLOCK_SIZE;
	assert(i > 0 && i < m->blockCount);
	return &m->block[i];
}

static int isInList(MemoryBlock *mb){
	return mb->prev != NULL;
}

static MemoryBlock *getBuddy(MemoryBlockManager *m, MemoryBlock *b){
	int index = (b - m->block);
	int buddy = (index ^ (1 << (b->sizeOrder - MIN_BLOCK_ORDER)));
	if(buddy >= m->blockCount){
		return NULL;
	}
	return m->block + buddy;
}

static void removeFromList(MemoryBlock *b){
	*(b->prev) = b->next;
	if(b->next != NULL){
		b->next->prev = b->prev;
		b->next = NULL;
	}
	b->prev = NULL;
}

static void addToList(MemoryBlock *b, MemoryBlock **previous){
	b->prev = previous;
	b->next = *previous;
	if(b->next != NULL){
		b->next->prev = &b->next;
	}
	*(b->prev) = b;
}

static void *_allocateBlock(MemoryBlockManager *m, size_t size){
	void *r = NULL;
	acquireLock(&m->lock);
	if(size > MAX_BLOCK_SIZE){
		goto allocateBlock_retuen;
	}
	assert(size >= MIN_BLOCK_SIZE);
	size_t i, i2;
	for(i = MIN_BLOCK_ORDER; ((size_t)1 << i) < size; i++);
	for(i2 = i; 1; i2++){
		if(m->freeBlock[i2 - MIN_BLOCK_ORDER] != NULL)
			break;
		if(i2 == MAX_BLOCK_SIZE)
			goto allocateBlock_retuen;
	}
	MemoryBlock *const b = m->freeBlock[i2 - MIN_BLOCK_ORDER];
	removeFromList(b);
	while(i2 != i){
		// split b and get buddy
		b->sizeOrder--;
		MemoryBlock *b2 = getBuddy(m, b);
		assert(b2 != NULL);
		assert(isInList(b2) == 0 && b2->sizeOrder == b->sizeOrder);
		addToList(b2, &m->freeBlock[b2->sizeOrder - MIN_BLOCK_ORDER]);
		i2--;
	}
	m->freeSize -= (1 << i);
	r = (void*)getAddress(m, b);
	allocateBlock_retuen:
	releaseLock(&m->lock);
	return r;
}

static void _freeBlock(MemoryBlockManager *m, void *address){
	acquireLock(&m->lock);
	MemoryBlock *b = getBlock(m, (uintptr_t)address);
	m->freeSize += (1 << b->sizeOrder);
	assert(isInList(b) == 0);
	while(b->sizeOrder < MAX_BLOCK_ORDER){
		MemoryBlock *b2 = getBuddy(m, b);
		if(b2 == NULL)
			break;
		assert(b2->sizeOrder <= b->sizeOrder);
		if(isInList(b2) == 0)
			break;
		// merge
		assert(b2->sizeOrder == b->sizeOrder);
		removeFromList(b2);
		assert(((uintptr_t)getAddress(m,b2) ^ (uintptr_t)getAddress(m,b)) == ((uintptr_t)1 << b->sizeOrder));
		b = (getAddress(m, b) < getAddress(m, b2)? b: b2);
		b->sizeOrder++;
	}
	addToList(b, &m->freeBlock[b->sizeOrder - MIN_BLOCK_ORDER]);
	releaseLock(&m->lock);
}

static MemoryBlockManager *createMemoryBlockManager(MemoryManager *m, uintptr_t maxAddr){
	MemoryBlockManager *bm = _allocateFixed(m, sizeof(*bm));
	bm->lock = initialSpinlock;
	bm->blockCount = maxAddr / MIN_BLOCK_SIZE;
	bm->freeSize = 0;
	bm->block = _allocateFixed(m, bm->blockCount * sizeof(*bm->block));
	int b;
	for(b = 0; b < bm->blockCount; b++){
		initMemoryBlock(&bm->block[b]); // all blocks are using in the beginning
	}
	for(b = 0; b <= MAX_BLOCK_ORDER - MIN_BLOCK_ORDER; b++){
		bm->freeBlock[b] = NULL;
	}
	return bm;
}

// kernel MemoryBlockManager
static MemoryBlockManager *kbm = NULL;

void *allocateBlock(size_t size){
	assert(kbm != NULL);
	return _allocateBlock(kbm, size);
}

void freeBlock(void *address){
	assert(kbm != NULL);
	_freeBlock(kbm, address);
}

static void initKernelMemoryBlock(MemoryManager *m){
	kbm = createMemoryBlockManager(m, findMaxAddress());
	AddressRange extraAR[2] = {
		{m->base, m->size, RESERVED, 0},
		{0, 1 << 20, RESERVED, 0}
	};
	int b;
	for(b = 0; b < kbm->blockCount; b++){
		int isFreeBlock = 1;
		uintptr_t blockBegin = b * MIN_BLOCK_SIZE;
		assert(blockBegin + MIN_BLOCK_SIZE > blockBegin);
		int i;
		for(i = 0; isFreeBlock && i < addressRangeCount + (int)LENGTH_OF(extraAR); i++){
			const AddressRange *ar = (i < addressRangeCount?
				addressRange + i:
				extraAR + (i - addressRangeCount)
			);
			if(ar->type != USABLE && !(
			ar->base >= blockBegin + MIN_BLOCK_SIZE || ar->base + ar->size <= blockBegin
			)){
				isFreeBlock = 0;
			}
		}
		if(isFreeBlock){
			_freeBlock(kbm, (void*)blockBegin);
		}
	}
}

// global memory manager
static MemoryManager *km = NULL;

size_t getAllocatedSize(void){
	return km->usedSize;
}

void *allocateAligned(size_t size, size_t align){
	assert(km != NULL);
	return _allocateAligned(km, size, align);
}

void *allocateFixed(size_t size){
	assert(km != NULL);
	return _allocateFixed(km, size);
}

void initKernelMemory(void){
	// find first usable memory address >= 1MB
	assert(km == NULL);
	size_t manageSize = (15 << 20);
	uintptr_t manageBase = findFirstUsableMemory(manageSize);
	km = createMemoryManager(manageBase, manageSize);
	initKernelMemoryBlock(km);
}

/*
void enablePaging(MemoryManager *m){
	setCR3((uint32_t)(m->pageDirectory));
	setCR0(getCR0() | 0x80000000);
}
*/
/*
#ifndef NDEBUG
void testMemoryBlock(BlockManager *b){
	void *a1,*a2,*a3,*a4;
	//void *fr1;
	a1 = allocateBlock(b, MIN_BLOCK_SIZE);
	freeBlock(b, a1);
	a2 = allocateBlock(b, MIN_BLOCK_SIZE);
	//assert(a1==a2);
	//fr1 = b->freeBlock[1];
	a3 = allocateBlock(b, MIN_BLOCK_SIZE+1);
	//assert(fr1 != b->freeBlock[1]);
	a4 = allocateBlock(b, MIN_BLOCK_SIZE);
	freeBlock(b,a4);
	freeBlock(b,a2);
	freeBlock(b,a3);
	//kprintf("%x %x %x %x %x\n",a1,a2,a3, MIN_BLOCK_SIZE+(uintptr_t)a3,a4);
}
#endif
*/
