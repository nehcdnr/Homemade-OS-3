#include"common.h"
#include"memory.h"
#include"page.h"
#include"assembly/assembly.h"
#include"multiprocessor/spinlock.h"
enum AddressRangeType{
	USABLE = 1,
	RESERVED = 2,
	ACPI_RECLAIMABLE = 3,
	ACPI_NVS = 4,
	BAD_MEMORY = 5
};
static_assert(sizeof(enum AddressRangeType) == 4);

typedef struct AddressRange{
	unsigned int base0_32;
	unsigned int base32_64;
	unsigned int size0_32;
	unsigned int size32_64;
	enum AddressRangeType type;
	unsigned int extra;
}AddressRange;
static_assert(sizeof(AddressRange) == 24);

extern const AddressRange *const addressRange;
extern const int addressRangeCount;

struct MemoryManager{
	unsigned base;
	unsigned size;
	unsigned usedSize;
	Spinlock *lock;
};

static MemoryManager *createNoLockMemoryManager(unsigned base, unsigned size){
	MemoryManager *m = (MemoryManager*)base;
	m->base = base;
	m->size = size;
	m->usedSize = sizeof(MemoryManager);
	m->lock = NULL;
	return m;
}

MemoryManager *createMemoryManager(unsigned base, unsigned size){
	MemoryManager *m = createNoLockMemoryManager(base, size);
	m->lock = createSpinlock(m);
	return m;
}

#define PADDING_SIZE(VALUE, ALIGN) ((VALUE) % (ALIGN) == 0? 0: (ALIGN) - (VALUE) % (ALIGN))

static unsigned getMaxAddress32(const AddressRange* ar){
	if(ar->size32_64 != 0 || ar->base0_32 > 0xffffffff - ar->size0_32){
		return 0xffffffff;
	}
	else{
		return ar->base0_32 + ar->size0_32 - 1;
	}
}

MemoryManager *initKernelMemoryManager(void){
	int i;
	// find first usable memory address >= 1MB
	unsigned manageBase = 0xffffffff;
	const unsigned manageSize = (8 << 20);
	// printf("%d memory address ranges\n", addressRangeCount);
	for(i = 0; i < addressRangeCount; i++){
		const AddressRange *ar = addressRange + i;

		printf("type: %d base: %x %x size: %x %x\n", ar->type,
		ar->base32_64, ar->base0_32,
		ar->size32_64, ar->size0_32);

		if(
		ar->type == USABLE &&
		ar->base32_64 == 0 && // address < 4GB
		ar->base0_32 >= (1 << 20) && // address >= 1MB
		(ar->size32_64 != 0 || ar->size0_32 >= manageSize) && // size >= 8MB
		ar->base0_32 < manageBase
		){
			manageBase = ar->base0_32;
		}
	}
	if(manageBase >= 0xffffffff - manageSize){
		panic("no memory for memory manager");
	}
	return createMemoryManager(manageBase, manageSize);
}

void *allocateAligned(MemoryManager *m, unsigned size, unsigned align){
	const unsigned padSize = PADDING_SIZE(m->base + m->usedSize, align);
	void *address = NULL;
	if(m->lock != NULL){
		acquireLock(m->lock);
	}
	if(size <= m->size - padSize - m->usedSize){
		address = (void*)(m->base + m->usedSize + padSize);
		m->usedSize += size + padSize;
	//printf("allocate = %u, base = %u\n", size, m->base+m->usedSize);
	}
	if(m->lock != NULL){
		releaseLock(m->lock);
	}
	assert(address != NULL);
	return address;
}

#define DEFAULT_ALIGN (4)

void *allocate(MemoryManager *m, unsigned size){
	return allocateAligned(m, size, DEFAULT_ALIGN);
}

unsigned getAllocatedSize(MemoryManager *m){
	return m->usedSize;
}

#define PAGE_UNIT_SIZE (1<<12)
struct PageManager{
	int pageCount, freePageCount;
	// pages[0 ~ freePageCount] are free pages
	// pages[freePageCount ~ pageCount] are using pages
	struct PageAllocation{
		unsigned address;
	}*pages;
};

PageManager *initKernelPageManager(MemoryManager *m){
	PageManager *p = allocate(m, sizeof(PageManager));
	p->pageCount = 0;
	p->freePageCount = 0;
	int iter;
	for(iter = 0; iter < 2; iter++){
		int usingPageCount = 0; // iter 1
		int i;
		for(i = 0; i < addressRangeCount; i++){
			const AddressRange *ar = addressRange + i;
			if(
			ar->type != USABLE ||
			ar->base32_64 != 0 ||
			ar->base32_64 > 0xffffffff - (PAGE_UNIT_SIZE - 1) ||
			(ar->size0_32 == 0 && ar->size32_64 == 0)
			){
				continue;
			}
			unsigned base = ar->base0_32 + PADDING_SIZE(ar->base0_32, PAGE_UNIT_SIZE);
			unsigned maxAddress = getMaxAddress32(ar);
			while(base <= maxAddress - (PAGE_UNIT_SIZE - 1)){
				if(iter == 0){
					p->pageCount++;
				}
				else{
					if(base >= (1<<20) && (
						base >= m->base + m->size ||
						maxAddress < m->base)
					){ // page and kernel not overlap
						p->pages[p->freePageCount].address = base;
						p->freePageCount++;
					}
					else{
						usingPageCount++;
						p->pages[p->pageCount - usingPageCount].address = base;
					}
				}
				if(base == maxAddress - (PAGE_UNIT_SIZE - 1))
					break;;
				base += PAGE_UNIT_SIZE;
			}
		}
		if(iter == 0){
			p->pages = allocate(m, p->pageCount * sizeof(struct PageAllocation));
		}
		assert(iter == 0 || usingPageCount + p->freePageCount == p->pageCount);
	}
	return p;
}

unsigned getUsingSize(PageManager *p){
	return (p->pageCount - p->freePageCount) * PAGE_UNIT_SIZE;
}
unsigned getUsableSize(PageManager *p){
	return p->pageCount * PAGE_UNIT_SIZE;
}

/*
void enablePaging(MemoryManager *m){
	setCR3((unsigned)(m->pageDirectory));
	setCR0(getCR0() | 0x80000000);
}
*/
