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
static void initKernelPDE(volatile PageDirectoryEntry *targetPDE, PageTable *pt){
	PageDirectoryEntry pde;
	pde.writable = 1;
	pde.userAccessible = 0;
	pde.writeThrough = 0;
	pde.cacheDisabled = 0;
	pde.accessed = 0;
	pde.zero1 = 0;
	pde.size4MB = 0;
	pde.zero2 = 0;
	pde.unused = 0;
	setPageDirectoryEntryAddress(&pde, pt);
	pde.present = 1;
	*(targetPDE) = pde;

}

#define PT_INDEX(ADDRESS) (((ADDRESS) >> 12) & (PAGE_TABLE_LENGTH - 1))
static void initKernelPTE(volatile PageTableEntry *targetPTE, void *physicalAddress){
	PageTableEntry pte;
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
	pte.present = 1;
	(*targetPTE) = pte;
}

static void invalidatePTE(volatile PageTableEntry *targetPTE, uintptr_t linearAddress){
	/*volatile int*a=(int*)linearAddress;
	if(*a!=0)printk("%x\n",*a);*/
	(*(volatile uint32_t*)targetPTE) = 0;
	invlpg((void*)linearAddress);
	/*if(*a!=0){
		printk(" %x\n",*a);
		//__asm__("hlt");
	}*/
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
	"orl $0x80000000, %%eax\n"
	"mov %%eax, %%cr0\n"
	:
	:
	:"eax"
	);
}

// kernel page table

struct TopLevelPageTable{
	PageDirectory pd;
	PageTable pt[PAGE_TABLE_LENGTH];
};

void *translatePage(TopLevelPageTable *p, void *linearAddress){
	uintptr_t i1 = PD_INDEX((uintptr_t)linearAddress);
	uintptr_t i2 = PT_INDEX((uintptr_t)linearAddress);
	if(isPDEPresent(p->pd.entry + i1) == 0){
		panic("translatePage");
	}
	return getPageTableEntryAddress(p->pt[i1].entry + i2);
}

int mapKernelPage(TopLevelPageTable *p, uintptr_t linearAddress, uintptr_t physicalAddress){
	assert((physicalAddress & 4095) == 0 && (linearAddress & 4095) == 0);
	uintptr_t i1 = PD_INDEX(linearAddress);
	uintptr_t i2 = PT_INDEX(linearAddress);
	volatile PageDirectoryEntry *pde = p->pd.entry + i1;
	PageTable *pt = p->pt + i1;
	if(isPDEPresent(pde) == 0){
		MEMSET0(pt);
	}
	assert((((uintptr_t)pt) & 4095) == 0);

	volatile PageTableEntry *pte = pt->entry + i2;
	assert(pte->present == 0);
	initKernelPTE(pte, (void*)physicalAddress);
	assert(pte->present == 1);
	if(isPDEPresent(pde) == 0){
		initKernelPDE(pde, pt);
	}

	return 1;
}

void unmapPage(TopLevelPageTable *p, uintptr_t linearAddress){
	uintptr_t i1 = PD_INDEX(linearAddress);
	uintptr_t i2 = PT_INDEX(linearAddress);
	PageTable *pt = getPageDirectoryEntryAddress(p->pd.entry + i1);
	assert(isPDEPresent(p->pd.entry + i1));
	if(isPTEPresent(pt->entry + i2) == 0){
		panic("page not present");
	}
	invalidatePTE(pt->entry + i2, linearAddress);
}

TopLevelPageTable *initKernelPageTable(uintptr_t manageBase, uintptr_t manageBegin, uintptr_t manageEnd){
	if(manageBegin % PAGE_TABLE_SIZE != 0){
		manageBegin += PAGE_TABLE_SIZE - (manageBegin % PAGE_TABLE_SIZE);
	}
	if(manageBegin + sizeof(TopLevelPageTable) > manageEnd){
		panic("no memory for memory manager");
	}
	TopLevelPageTable *kPage = (TopLevelPageTable*)manageBegin;
	MEMSET0(&(kPage->pd));
	// MEMSET0(kPage->pt);
	uintptr_t a;
	for(a = manageBase; a < manageEnd; a += PAGE_SIZE * PAGE_TABLE_LENGTH){
		uintptr_t pdIndex = PD_INDEX(a);
		PageTable *kpt = kPage->pt + pdIndex;
		MEMSET0(kpt);
		initKernelPDE(kPage->pd.entry + pdIndex, (void*)((uintptr_t)kpt));
	}
	for(a = manageBase; a < manageEnd; a += PAGE_SIZE){
		uintptr_t pdIndex = PD_INDEX(a);
		uintptr_t ptIndex = PT_INDEX(a);
		initKernelPTE(kPage->pt[pdIndex].entry + ptIndex, (void*)a);
	}
	setCR3(&(kPage->pd)); // TODO: linker script
	setCR0PagingBit();

	return kPage;
}
