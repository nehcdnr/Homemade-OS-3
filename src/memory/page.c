#include"common.h"
#include"memory.h"
#include"page.h"

#define PAGE_TABLE_SIZE (4096)
#define PAGE_TABLE_LENGTH (1024)
typedef struct PageDirectory{
	struct PageDirectoryEntry{
		uint8_t present: 1;
		uint8_t writable: 1;
		uint8_t userAccessible: 1;
		uint8_t writeThrough: 1;
		uint8_t cacheDisabled: 1;
		uint8_t accessed: 1;
		uint8_t ignored1: 1;
		uint8_t size4MB: 1;
		uint8_t ignored2: 1;
		uint8_t unused: 3;
		uint8_t address0_4: 4;
		uint16_t address4_20: 16;
	}entry[PAGE_TABLE_LENGTH];
}PageDirectory;

typedef struct PageTable{
	struct PageTableEntry{
		uint8_t present: 1;
		uint8_t writable: 1;
		uint8_t userAccessible: 1;
		uint8_t writeThrough: 1;
		uint8_t cacheDisabled: 1;
		uint8_t accessed: 1;
		uint8_t dirty: 1;
		uint8_t ignored: 1;
		uint8_t global: 1;
		uint8_t unused: 3;
		uint8_t address0_4: 4;
		uint16_t address4_20: 16;
	}entry[PAGE_TABLE_LENGTH];
}PageTable;


static_assert(sizeof(struct PageDirectoryEntry) == 4);
static_assert(sizeof(struct PageTableEntry) == 4);
static_assert(sizeof(PageDirectory) == PAGE_TABLE_SIZE);
static_assert(sizeof(PageTable) == PAGE_TABLE_SIZE);
static_assert(PAGE_TABLE_SIZE % MIN_BLOCK_SIZE == 0);

PageDirectory *createPageDirectory(void){
	PageDirectory *pd = allocateBlock(sizeof(PageDirectory));
	if(pd == NULL)
		return NULL;
	MEMSET0(pd);
	return pd;
}

static PageTable *createPageTable(void){
	PageTable *pt = allocateBlock(sizeof(PageTable));
	if(pt == NULL)
		return NULL;
	MEMSET0(pt);
	return pt;
}

#define SET_ENTRY_ADDRESS(E, A) ((*(uint32_t*)(E)) = (((*(uint32_t*)(E)) & (4095)) | ((uint32_t)(A))))
#define GET_ENTRY_ADDRESS(E) ((void*)((*(uint32_t*)(E)) & (~4095)))

void map4KBKernelPage(PageDirectory *pd, uintptr_t linearAddress, uintptr_t physicalAddress){
	assert((physicalAddress & 4095) == 0 && (linearAddress & 4095) == 0);
	uintptr_t i1 = ((linearAddress >> 22) & (PAGE_TABLE_LENGTH - 1));
	uintptr_t i2 = ((linearAddress >> 12) & (PAGE_TABLE_LENGTH - 1));
	struct PageDirectoryEntry *pde = pd->entry + i1;
	PageTable *pt;
	struct PageTableEntry *pte;
	if(pde->present == 0){
		pt = createPageTable();
	}
	else{
		pt = GET_ENTRY_ADDRESS(pde);
	}
	assert((((uintptr_t)pt) & 4095) == 0);
	// assert(pte->present == 0);
	pte = pt->entry + i2;
	pte->writable = 1;
	pte->userAccessible = 0;
	pte->writeThrough = 0;
	pte->cacheDisabled = 0;
	pte->accessed = 0;
	pte->dirty = 0;
	pte->ignored = 0;
	pte->global = 0;
	pte->unused = 0;
	SET_ENTRY_ADDRESS(pte, physicalAddress);
	pte->present = 1;

	if(pde->present == 0){
		pde->writable = 1;
		pde->userAccessible = 0;
		pde->writeThrough = 0;
		pde->cacheDisabled = 0;
		pde->accessed = 0;
		pde->ignored1 = 0;
		pde->size4MB = 0;
		pde->ignored2 = 0;
		pde->unused = 0;
		SET_ENTRY_ADDRESS(pde, pt);
		pde->present = 1;
	}
}
