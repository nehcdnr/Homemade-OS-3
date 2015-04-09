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

// BlockManager functions
typedef struct MemoryBlock{
	unsigned sizeOrder;
	struct MemoryBlock**prev, *next;
}MemoryBlock;

#define PAGE_UNIT_SIZE MIN_BLOCK_SIZE
typedef struct MemoryBlockManager{
	size_t sizeOfThis;
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

static int isInFreeList(MemoryBlock *mb){
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



static void *_allocateBlock(MemoryBlockManager *m, size_t size){
	void *r = NULL;
	acquireLock(&m->lock);
	if(size > MAX_BLOCK_SIZE){
		goto allocateBlock_return;
	}
	// assert(size >= MIN_BLOCK_SIZE);
	size_t i, i2;
	for(i = MIN_BLOCK_ORDER; ((size_t)1 << i) < size; i++);
	for(i2 = i; 1; i2++){
		if(m->freeBlock[i2 - MIN_BLOCK_ORDER] != NULL)
			break;
		if(i2 == MAX_BLOCK_ORDER)
			goto allocateBlock_return;
	}
	MemoryBlock *const b = m->freeBlock[i2 - MIN_BLOCK_ORDER];
	REMOVE_FROM_DQUEUE(b);
	while(i2 != i){
		// split b and get buddy
		b->sizeOrder--;
		MemoryBlock *b2 = getBuddy(m, b);
		assert(b2 != NULL);
		assert(isInFreeList(b2) == 0 && b2->sizeOrder == b->sizeOrder);
		ADD_TO_DQUEUE(b2, &m->freeBlock[b2->sizeOrder - MIN_BLOCK_ORDER]);
		i2--;
	}
	m->freeSize -= (1 << i);
	r = (void*)getAddress(m, b);
	allocateBlock_return:
	releaseLock(&m->lock);
	return r;
}

static void _freeBlock(MemoryBlockManager *m, void *memory){
	acquireLock(&m->lock);
	MemoryBlock *b = getBlock(m, (uintptr_t)memory);
	m->freeSize += (1 << b->sizeOrder);
	assert(isInFreeList(b) == 0);
	while(b->sizeOrder < MAX_BLOCK_ORDER){
		MemoryBlock *buddy = getBuddy(m, b);
		if(buddy == NULL)
			break;
		assert(buddy->sizeOrder <= b->sizeOrder);
		if(isInFreeList(buddy) == 0 || buddy->sizeOrder != b->sizeOrder)
			break;
		// merge
		//printk("%d %d\n",buddy->sizeOrder, b->sizeOrder);
		REMOVE_FROM_DQUEUE(buddy);
		assert(((uintptr_t)getAddress(m,buddy) ^ (uintptr_t)getAddress(m,b)) == ((uintptr_t)1 << b->sizeOrder));
		b = (getAddress(m, b) < getAddress(m, buddy)? b: buddy);
		b->sizeOrder++;
	}
	ADD_TO_DQUEUE(b, &m->freeBlock[b->sizeOrder - MIN_BLOCK_ORDER]);
	releaseLock(&m->lock);
}

static MemoryBlockManager *createMemoryBlockManager(uintptr_t manageBase, size_t manageSize, uintptr_t maxAddr){
	uintptr_t m = manageBase;
	while(m % 4 != 0){ // align to 4
		m++;
	}
	MemoryBlockManager *bm = (MemoryBlockManager*)m;
	m += sizeof(MemoryBlockManager);
	bm->blockCount = maxAddr / MIN_BLOCK_SIZE;
	bm->freeSize = 0;
	bm->lock = initialSpinlock;
	bm->block = (MemoryBlock*)m;
	m += sizeof(MemoryBlock) * bm->blockCount;
	bm->sizeOfThis = m - manageBase;
	if(bm->sizeOfThis > manageSize){
		panic("memory manager initialization error");
	}
	int b;
	for(b = 0; b < bm->blockCount; b++){
		 // all blocks are using and in MIN_BLOCK_ORDER in the beginning
		initMemoryBlock(&bm->block[b]);
	}
	for(b = 0; b <= MAX_BLOCK_ORDER - MIN_BLOCK_ORDER; b++){
		bm->freeBlock[b] = NULL;
	}
	return bm;
}

// kernel MemoryBlockManager
static MemoryBlockManager *initKernelMemoryBlock(uintptr_t manageBase, size_t manageSize){
	static MemoryBlockManager *kbm = NULL;
	kbm = createMemoryBlockManager(manageBase, manageSize, findMaxAddress());
	AddressRange extraAR[2] = {
		{((uintptr_t)kbm), kbm->sizeOfThis, RESERVED, 0},
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
	return kbm;
}


// MemoryManager functions

typedef union MemoryUnit{
	union MemoryUnit *next;
	int address[0];
}MemoryUnit;

static_assert(sizeof(MemoryUnit) == 4);

// POOL_SIZE must be multiple of MIN_BLOCK_SIZE
#define POOL_SIZE (MIN_BLOCK_SIZE)

typedef struct Pool{
	struct Pool *next, **prev;
	size_t usedCount;
	MemoryUnit *freeList;
}MemoryPool;

static int isTotallyFree(MemoryPool *p){
	return p->usedCount == 0;
}

static int isTotallyUsed(MemoryPool *p){
	return p->freeList == NULL;
}

static MemoryPool *createMemoryPool(MemoryBlockManager *bm, size_t unit){
	MemoryPool *pool = (MemoryPool*)_allocateBlock(bm, POOL_SIZE);
	pool->prev = NULL;
	pool->next = NULL;
	pool->usedCount = 0;
	uintptr_t p = ((uintptr_t)pool);
	p += sizeof(MemoryPool);
	MemoryUnit *fl = NULL;
	while(p + sizeof(MemoryUnit) + unit <= ((uintptr_t)pool) + POOL_SIZE){
		MemoryUnit *u = (MemoryUnit*)p;
		u->next = fl;
		fl = u;
		p = p + sizeof(MemoryUnit) + unit;
	}
	pool->freeList = fl;
	return pool;
}

static void *allocateUnit(MemoryPool *p){
	MemoryUnit *m = p->freeList;
	if(m == NULL)
		return NULL;
	p->freeList = m->next;
	p->usedCount++;
	return m->address;
}

static_assert((POOL_SIZE & (POOL_SIZE - 1)) == 0);
static MemoryPool *freeUnit(void *address){
	uintptr_t a = (uintptr_t)address;
	MemoryUnit *u = address;
	MemoryPool *p = (MemoryPool*)(a - (a & (POOL_SIZE - 1)));
	u->next = p->freeList;
	p->freeList = u;
	p->usedCount--;
	return p;
}

// memory pool Manager
static const size_t poolUnit[] = {
	16,
	32,
	64,
	128 - sizeof(MemoryPool),
	256 - sizeof(MemoryPool),
	512 - sizeof(MemoryPool),
	1024 - sizeof(MemoryPool),
	2048 - sizeof(MemoryPool)
};
#define NUMBER_OF_POOL_UNIT (LENGTH_OF(poolUnit))
static_assert(NUMBER_OF_POOL_UNIT == 8);

typedef struct MemoryManager{
	Spinlock lock;
	MemoryPool *usablePool[NUMBER_OF_POOL_UNIT];
	MemoryPool *usedPool[NUMBER_OF_POOL_UNIT];

	MemoryBlockManager *bm;
}MemoryManager;

static int findMemoryPool(MemoryManager *m, size_t size){
	int i;
	for(i = 0; 1; i++){
		if((unsigned)i >= NUMBER_OF_POOL_UNIT){
			panic("error allocating memory");
		}
		if(poolUnit[i] >= size){
			break;
		}
	}
	acquireLock(&(m->lock));
	MemoryPool *p = m->usablePool[i];
	releaseLock(&m->lock);
	if(p != NULL){
		return i;
	}

	p = createMemoryPool(m->bm, poolUnit[i]);
	if(p == NULL){
		return -1;
	}
	acquireLock(&(m->lock));
	ADD_TO_DQUEUE(p, m->usablePool + i);
	releaseLock(&m->lock);
	return i;
}

static void *_allocate(MemoryManager *m, size_t size){
	if(size >= poolUnit[NUMBER_OF_POOL_UNIT - 1]){
		return _allocateBlock(m->bm, size);
	}
	int i = findMemoryPool(m, size);
	if(i < 0)
		return NULL;
	MemoryPool *p = m->usablePool[i];
	if(p == NULL){
		return NULL;
	}
	void *r;
	acquireLock(&m->lock);
	r = allocateUnit(p);
	assert(r != NULL);
	if(isTotallyUsed(p)){
		REMOVE_FROM_DQUEUE(p);
		ADD_TO_DQUEUE(p, m->usedPool + i);
	}
	releaseLock(&(m->lock));
	assert(((uintptr_t)r) % MIN_BLOCK_SIZE != 0);
	return r;
}

static void _free(MemoryManager *m, void *address){
	uintptr_t a = (uintptr_t)address;
	if(a % MIN_BLOCK_SIZE == 0){
		_freeBlock(m->bm, address);
		return;
	}
	acquireLock(&m->lock);
	MemoryPool *p = freeUnit(address);
	if(isTotallyFree(p)){
		REMOVE_FROM_DQUEUE(p);
		releaseLock(&m->lock);
		_freeBlock(m->bm, p);
	}
	else{
		releaseLock(&m->lock);
	}
}

static MemoryManager *initKernelMemoryManager(MemoryBlockManager *bm){
	size_t unit;
	unsigned int i;
	for(i = 0; poolUnit[i] < sizeof(MemoryManager); i++);
	assert(i < NUMBER_OF_POOL_UNIT);
	unit = poolUnit[i];
	MemoryPool *s = createMemoryPool(bm, unit);

	MemoryManager *m = allocateUnit(s);
	m->lock = initialSpinlock;
	m->bm = bm;

	for(i = 0; i < NUMBER_OF_POOL_UNIT; i++){
		m->usablePool[i] = NULL;
		m->usedPool[i] = NULL;
		if(poolUnit[i] == unit){
			ADD_TO_DQUEUE(s, m->usablePool + i);
		}
	}
	return m;
}

// global memory manager
static MemoryManager *km = NULL;

void *allocate(size_t size){
	assert(km != NULL);
	return _allocate(km, size);
}

void *allocateBlock(size_t size){
	assert(km != NULL && km->bm != NULL);
	return _allocateBlock(km->bm, size);
}

void free(void *memory){
	assert(km != NULL);
	_free(km, memory);
}

/*
void enablePaging(MemoryManager *m){
	setCR3((uint32_t)(m->pageDirectory));
	setCR0(getCR0() | 0x80000000);
}
*/

/*
#ifndef NDEBUG
#define TEST_N (70)
static void testMemoryManager(void){
	uint8_t *p[TEST_N];
	int si[TEST_N];
	unsigned int r;
	int a, b, c;
	r=MIN_BLOCK_SIZE + 388;
	for(b=0;b<50;b++){
		for(a=0;a<TEST_N;a++){
			si[a]=r;
			p[a]=allocate(r);
			//printk("%d %d %x\n",a,r,p[a]);
			if(p[a] == NULL){
				printk("a = %d, r = %d p[a] = %x\n", a, r, p[a]);
				panic("mem test checkpoint 1");
			}
			for(c=0;c<si[a]&&c<100;c++){
				p[a][c] =
				p[a][si[a]-c-1]= a+1;
			}
			//r = 1 + (r*7 + 3) % (30 - 1);
			r = (r*79+3);
			if(r%5<3) r = r % 2048;
			else r = (r*17) % (MAX_BLOCK_SIZE - MIN_BLOCK_SIZE) + MIN_BLOCK_SIZE;
		}
		for(a=0;a<TEST_N;a++){
			int a2 = (a+r)%TEST_N;
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
	hlt();
	//kprintf("%x %x %x %x %x\n",a1,a2,a3, MIN_BLOCK_SIZE+(uintptr_t)a3,a4);
}
#endif
*/
void initKernelMemory(void){
	// find first usable memory address >= 1MB
	assert(km == NULL);
	size_t manageSize = (15 << 20);
	uintptr_t manageBase = findFirstUsableMemory(manageSize);
	// km = createMemoryManager(manageBase, manageSize);
	MemoryBlockManager *bm = initKernelMemoryBlock(manageBase, manageSize);
	km = initKernelMemoryManager(bm);
	/*
	#ifndef NDEBUG
	testMemoryManager();
	#endif
	*/
}
