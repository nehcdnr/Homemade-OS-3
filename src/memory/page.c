#include"common.h"
#include"memory.h"
#include"memory_private.h"
#include"page.h"

#define PAGE_TABLE_SIZE (4096)
#define PAGE_TABLE_LENGTH (1024)

typedef struct{
	uint8_t present: 1;
	uint8_t writable: 1;
	uint8_t userAccessible: 1;
	uint8_t writeThrough: 1;
	uint8_t cacheDisabled: 1;
	uint8_t accessed: 1;
	uint8_t dirty: 1;
	uint8_t zero: 1;
	uint8_t global: 1;
	uint8_t unused: 3;
	uint8_t address0_4: 4;
	uint16_t address4_20: 16;
}PageTableEntry;
typedef struct PageTable{
	volatile PageTableEntry entry[PAGE_TABLE_LENGTH];
}PageTable;

typedef struct{
	uint8_t present: 1;
	uint8_t writable: 1;
	uint8_t userAccessible: 1;
	uint8_t writeThrough: 1;
	uint8_t cacheDisabled: 1;
	uint8_t accessed: 1;
	uint8_t zero1: 1;
	uint8_t size4MB: 1;
	uint8_t zero2: 1;
	uint8_t unused: 3;
	uint8_t address0_4: 4;
	uint16_t address4_20: 16;
}PageDirectoryEntry;
typedef struct PageDirectory{
	volatile PageDirectoryEntry entry[PAGE_TABLE_LENGTH];
}PageDirectory;

static_assert(sizeof(PageDirectoryEntry) == 4);
static_assert(sizeof(PageTableEntry) == 4);
static_assert(sizeof(PageDirectory) == PAGE_TABLE_SIZE);
static_assert(sizeof(PageTable) == PAGE_TABLE_SIZE);
static_assert(PAGE_TABLE_SIZE % MIN_BLOCK_SIZE == 0);
/*
static PageDirectory *_createPageDirectory(MemoryBlockManager *m){
	size_t sizePD = sizeof(PageDirectory);
	PageDirectory *pd = allocateBlock(m, &sizePD);
	if(pd == NULL)
		return NULL;
	MEMSET0(pd); // TO DO: physical
	return pd;
}

static PageTable *_createPageTable(MemoryBlockManager *m){
	size_t sizePT =  sizeof(PageTable);
	PageTable *pt = allocateBlock(m, &sizePT);
	if(pt == NULL)
		return NULL;
	MEMSET0(pt); // TO DO: physical
	return pt;
}

TopLevelPageTable *createPageTable(MemoryBlockManager *m){
	return _createPageDirectory(m);
}
*/
#define SET_ENTRY_ADDRESS(E, A) ((*(uint32_t*)(E)) = (((*(uint32_t*)(E)) & (4095)) | ((uint32_t)(A))))
#define GET_ENTRY_ADDRESS(E) ((void*)((*(uint32_t*)(E)) & (~4095)))

static void* getPageTableEntryAddress(volatile PageTableEntry *e){
	return GET_ENTRY_ADDRESS(e);
}

static void setPageTableEntryAddress(PageTableEntry *e, void *a){
	SET_ENTRY_ADDRESS(e, a);
}

static PageTable *getPageDirectoryEntryAddress(volatile PageDirectoryEntry *e){
	return GET_ENTRY_ADDRESS(e);
}

static void setPageDirectoryEntryAddress(PageDirectoryEntry *e, PageTable *a){
	SET_ENTRY_ADDRESS(e, a);
}

#undef GET_ENTRY_ADDRESS
#undef SET_ENTRY_ADDRESS
/*
static void deletePageTable(MemoryBlockManager *m, PageTable *pt){
	unsigned int i;
	for(i = 0; i < LENGTH_OF(pt->entry); i++){
		if(pt->entry[i].present == 0){
			continue;
		}
		releaseBlock(m, getPageTableEntryAddress(pt->entry+i));
	}
	releaseBlock(m, pt);
}

void deletePageDirectory(MemoryBlockManager *m, PageDirectory *pd){
	unsigned int i;
	static_assert(LENGTH_OF(pd->entry) == PAGE_TABLE_LENGTH);
	for(i = 0; i < LENGTH_OF(pd->entry); i++){
		if(pd->entry[i].present == 0)
			continue;
		deletePageTable(m, getPageDirectoryEntryAddress(pd->entry + i));
	}
	releaseBlock(m, pd);
}
*/
static void invlpg(void *linearAddress){
	__asm__(
	"invlpg %0\n"
	:
	:"m"(*(uint8_t*)linearAddress)
	);
}

// not necessary to invlpg when changing present flag from 0 to 1
#define PD_INDEX(ADDRESS) (((ADDRESS) >> 22) & (PAGE_TABLE_LENGTH - 1))
static void initKernelPDE(volatile PageDirectoryEntry *targetPDE, PageTable *pt_physical){
	PageDirectoryEntry pde;
	pde.present = 1;
	pde.writable = 1;
	pde.userAccessible = 0;
	pde.writeThrough = 0;
	pde.cacheDisabled = 0;
	pde.accessed = 0;
	pde.zero1 = 0;
	pde.size4MB = 0;
	pde.zero2 = 0;
	pde.unused = 0;
	setPageDirectoryEntryAddress(&pde, pt_physical);
	(*targetPDE) = pde;
}

#define PT_INDEX(ADDRESS) (((ADDRESS) >> 12) & (PAGE_TABLE_LENGTH - 1))
static void initKernelPTE(volatile PageTableEntry *targetPTE, void *physicalAddress){
	PageTableEntry pte;
	pte.present = 1;
	pte.writable = 1;
	pte.userAccessible = 0;
	pte.writeThrough = 0;
	pte.cacheDisabled = 0;
	pte.accessed = 0;
	pte.dirty = 0;
	pte.zero = 0;
	pte.global = 0;
	pte.unused = 0;
	setPageTableEntryAddress(&pte, physicalAddress);
	(*targetPTE) = pte;
}

static void invalidatePTE(volatile PageTableEntry *targetPTE, uintptr_t linearAddress){
	(*(volatile uint32_t*)targetPTE) = 0;
	invlpg((void*)linearAddress);
}

static int isPDEPresent(volatile PageDirectoryEntry *e){
	return e->present;
}

static int isPTEPresent(volatile PageTableEntry *e){
	return e->present;
}

PageDirectory *getCR3(void){
	PageDirectory *value;
	__asm__(
	"mov %%cr3, %0\n"
	:"=a"(value)
	:
	);
	return value;
}

void setCR3(PageDirectory *value){
	__asm__(
	"mov  %0, %%cr3\n"
	:
	:"a"(value)
	);
}

static void setCR0PagingBit(void){
	__asm__(
	"mov %%cr0, %%eax\n"
	"mov %%cr0, %%ecx\n"
	"orl $0x80000000, %%eax\n"
	"cmp %%eax, %%ecx\n"
	"je skipsetcr0\n"
	"mov %%eax, %%cr0\n"
	"skipsetcr0:"
	:
	:
	:"eax","ecx"
	);
}

// kernel page table

// pd->entry[i] map to pt[(i + ptIndexBase) % PAGE_TABLE_LENGTH]
// pdIndexBase = (4G-&pageManager)>>22, so that &pageManager is at pt[0]
struct TopLevelPageTable{
	int pdIndexBase;
	PageDirectory pd;
	PageTable pt[PAGE_TABLE_LENGTH];
};
#define PAGE_MANAGER_ALIGN (PAGE_TABLE_SIZE - (uintptr_t)&(((TopLevelPageTable*)0)->pd))

static PageTable *linearAddressOfPageTable(TopLevelPageTable *p, uintptr_t linear){
	int index = ((PD_INDEX(linear) + p->pdIndexBase) & (PAGE_TABLE_LENGTH - 1));
	return p->pt + index;
}

void *translatePage(TopLevelPageTable *p, void *linearAddress){
	uintptr_t i1 = PD_INDEX((uintptr_t)linearAddress);
	uintptr_t i2 = PT_INDEX((uintptr_t)linearAddress);
	if(isPDEPresent(p->pd.entry + i1) == 0){
		panic("translatePage");
	}
	PageTable *pt = linearAddressOfPageTable(p, (uintptr_t)linearAddress);
	return getPageTableEntryAddress(pt->entry + i2);
}

int mapKernelPage(
	LinearMemoryManager *m,
	uintptr_t linearAddress, uintptr_t physicalAddress
){
	struct TopLevelPageTable *p = m->page;
	assert((physicalAddress & 4095) == 0 && (linearAddress & 4095) == 0);
	uintptr_t i1 = PD_INDEX(linearAddress);
	uintptr_t i2 = PT_INDEX(linearAddress);
	PageTable *pt_linear = linearAddressOfPageTable(p, linearAddress);
	if(isPDEPresent(p->pd.entry + i1) == 0){
		size_t p_size = sizeof(PageTable);
		PageTable *pt_physical = allocateBlock(m->physical, &p_size);
		if(pt_physical == NULL){
			return 0;
		}
		initKernelPDE(p->pd.entry + i1, pt_physical);
#ifndef NDEBUG
		uintptr_t pt_i1 = PD_INDEX((uintptr_t)pt_linear);
		assert(isPDEPresent(p->pd.entry + pt_i1));
#endif
		mapKernelPage(m, (uintptr_t)pt_linear, (uintptr_t)pt_physical);
		MEMSET0(pt_linear);
	}
	assert((((uintptr_t)pt_linear) & 4095) == 0);

	volatile PageTableEntry *pte = pt_linear->entry + i2;
	//assert(pte->present == 0);
	initKernelPTE(pte, (void*)physicalAddress);
	assert(pte->present == 1);
((volatile int*)linearAddress)[0]=1;
	return 1;
}

void unmapPage(
	TopLevelPageTable *p, MemoryBlockManager *physical,
	uintptr_t linear
){
	uintptr_t i1 = PD_INDEX(linear);
	uintptr_t i2 = PT_INDEX(linear);
	PageTable *pt_linear = linearAddressOfPageTable(p, linear);
	PageTable *pt_physical = getPageDirectoryEntryAddress(p->pd.entry + i1);
	assert(isPDEPresent(p->pd.entry + i1));
	if(isPTEPresent(pt_linear->entry + i2) == 0){
		panic("page not present");
	}
	invalidatePTE(pt_linear->entry + i2, linear);
	if(0/*the PageTable is empty*/){
		releaseBlock(physical , pt_physical);
	}
}

TopLevelPageTable *initKernelPageTable(
	uintptr_t manageBase, uintptr_t manageBegin, uintptr_t manageEnd,
	uintptr_t kernelLinearBase, uintptr_t kernelLinearEnd
){
	assert(kernelLinearBase % (PAGE_SIZE * PAGE_TABLE_LENGTH) == 0 && kernelLinearEnd % PAGE_SIZE == 0);
	assert(manageBase == kernelLinearBase && manageEnd <= kernelLinearEnd);

	manageBegin += sizeof(TopLevelPageTable);
	if(PAGE_MANAGER_ALIGN >= manageBegin % PAGE_TABLE_SIZE){
		manageBegin += PAGE_MANAGER_ALIGN - (manageBegin % PAGE_TABLE_SIZE);
	}
	else{
		manageBegin += PAGE_MANAGER_ALIGN + (PAGE_TABLE_SIZE - manageBegin % PAGE_TABLE_SIZE);
	}
	TopLevelPageTable *kPage = (TopLevelPageTable*)manageBegin;
	assert(((uintptr_t)&kPage->pd) % PAGE_TABLE_SIZE == 0);

	kPage->pdIndexBase = PD_INDEX(kernelLinearBase);
	MEMSET0(&kPage->pd);
	uintptr_t a;
	for(a = manageBase; a < manageEnd; a += PAGE_SIZE * PAGE_TABLE_LENGTH){
		int pdIndex = PD_INDEX(a);
		PageTable *kpt = linearAddressOfPageTable(kPage, a);
		MEMSET0(kpt);
		initKernelPDE(kPage->pd.entry + pdIndex, (PageTable*)(((uintptr_t)kpt) - kernelLinearBase));
	}
	for(a = manageBase; a < manageEnd; a += PAGE_SIZE){
		PageTable *kpt = linearAddressOfPageTable(kPage, a);
		int ptIndex = PT_INDEX(a);
		initKernelPTE(kpt->entry + ptIndex, ((void*)(a - kernelLinearBase)));
	}
	setCR3(&kPage->pd);
	setCR0PagingBit();

	return kPage;
}
