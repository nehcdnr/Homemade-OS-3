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
	uint64_t base;
	uint64_t size;
	enum AddressRangeType type;
	uint32_t extra;
}AddressRange;
static_assert(sizeof(AddressRange) == 24);

extern const AddressRange *const addressRange;
extern const int addressRangeCount;

struct MemoryManager{
	uintptr_t base;
	size_t size;
	size_t usedSize;
	Spinlock *lock;
};

static MemoryManager *createNoLockMemoryManager(uintptr_t base, size_t size){
	MemoryManager *m = (MemoryManager*)base;
	m->base = base;
	m->size = size;
	m->usedSize = sizeof(MemoryManager);
	m->lock = NULL;
	return m;
}

MemoryManager *createMemoryManager(uintptr_t base, size_t size){
	MemoryManager *m = createNoLockMemoryManager(base, size);
	m->lock = createSpinlock(m);
	return m;
}

#define PADDING_SIZE(VALUE, ALIGN) ((VALUE) % (ALIGN) == 0? 0: (ALIGN) - (VALUE) % (ALIGN))

static uintptr_t getMaxAddress32(const AddressRange* ar){
	if(ar->base + ar->size > 0xffffffff){
		return 0xffffffff;
	}
	else{
		return ar->base + ar->size - 1;
	}
}

MemoryManager *initKernelMemoryManager(void){
	int i;
	// find first usable memory address >= 1MB
	uintptr_t manageBase = 0xffffffff;
	const size_t manageSize = (8 << 20);
	// printf("%d memory address ranges\n", addressRangeCount);
	for(i = 0; i < addressRangeCount; i++){
		const AddressRange *ar = addressRange + i;

		kprintf("type: %d base: %x %x size: %x %x\n", ar->type,
		ar->base >> 32, ar->base & 0xffffffff,
		ar->size >> 32, ar->size & 0xffffffff);

		if(
		ar->type == USABLE &&
		(ar->base >> 32) == 0 && // address < 4GB
		ar->base >= (1 << 20) && // address >= 1MB
		ar->size >= manageSize && // size >= 8MB
		ar->base < manageBase
		){
			manageBase = (uintptr_t)ar->base;
		}
	}
	if(manageBase >= 0xffffffff - manageSize){
		panic("no memory for memory manager");
	}
	return createMemoryManager(manageBase, manageSize);
}

void *allocateAligned(MemoryManager *m, size_t size, size_t align){
	const size_t padSize = PADDING_SIZE(m->base + m->usedSize, align);
	void *address = NULL;
	if(m->lock != NULL){
		acquireLock(m->lock);
	}
	if(size <= m->size - padSize - m->usedSize){
		address = (void*)(m->base + m->usedSize + padSize);
		m->usedSize += size + padSize;
	}
	if(m->lock != NULL){
		releaseLock(m->lock);
	}
	assert(address != NULL);
	return address;
}

#define DEFAULT_ALIGN ((size_t)4)

void *allocate(MemoryManager *m, size_t size){
	return allocateAligned(m, size, DEFAULT_ALIGN);
}

size_t getAllocatedSize(MemoryManager *m){
	return m->usedSize;
}

#define PAGE_UNIT_SIZE (1<<12)
struct PageManager{
	int pageCount, freePageCount;
	// pages[0 ~ freePageCount] are free pages
	// pages[freePageCount ~ pageCount] are using pages
	struct PageAllocation{
		uintptr_t address;
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
			ar->base > 0xffffffff - (PAGE_UNIT_SIZE - 1) ||
			ar->size < PAGE_UNIT_SIZE
			){
				continue;
			}
			uintptr_t base = ((uintptr_t)ar->base) + PADDING_SIZE(((uintptr_t)ar->base), PAGE_UNIT_SIZE);
			uintptr_t maxAddress = getMaxAddress32(ar);
			while(base <= maxAddress - (PAGE_UNIT_SIZE - 1)){
				if(iter == 0){
					p->pageCount++;
				}
				else{
					if(base >= (1<<20) && (
						base >= m->base + m->size ||
						maxAddress < m->base
					)){ // page and kernel not overlap
						p->pages[p->freePageCount].address = base;
						p->freePageCount++;
					}
					else{
						usingPageCount++;
						p->pages[p->pageCount - usingPageCount].address = base;
					}
				}
				if(base == maxAddress - (PAGE_UNIT_SIZE - 1))
					break;
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

size_t getUsingSize(PageManager *p){
	return PAGE_UNIT_SIZE * (size_t)(p->pageCount - p->freePageCount);
}
size_t getUsableSize(PageManager *p){
	return PAGE_UNIT_SIZE * (size_t)p->pageCount;
}

/*
void enablePaging(MemoryManager *m){
	setCR3((uint32_t)(m->pageDirectory));
	setCR0(getCR0() | 0x80000000);
}
*/
