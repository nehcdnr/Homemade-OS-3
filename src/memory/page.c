#include"common.h"
#include"memory.h"
#include"page.h"

#define PAGE_TABLE_SIZE (4096)
#define PAGE_TABLE_LENGTH (1024)
typedef struct PageDirectory{
	struct PageDirectoryEntry{
		char present: 1;
		char writable: 1;
		char userAccessible: 1;
		char writeThrough: 1;
		char cacheDisabled: 1;
		char accessed: 1;
		char ignored1: 1;
		char size4MB: 1;
		char ignored2: 1;
		unsigned char unused: 3;
		unsigned char address0_4: 4;
		unsigned short address4_20: 16;
	}entry[PAGE_TABLE_LENGTH];
}PageDirectory;

typedef struct PageTable{
	struct PageTableEntry{
		char present: 1;
		char writable: 1;
		char userAccessible: 1;
		char writeThrough: 1;
		char cacheDisabled: 1;
		char accessed: 1;
		char dirty: 1;
		char ignored: 1;
		char global: 1;
		unsigned char unused: 3;
		unsigned char address0_4: 4;
		unsigned short address4_20: 16;
	}entry[PAGE_TABLE_LENGTH];
}PageTable;


static_assert(sizeof(struct PageDirectoryEntry) == 4);
static_assert(sizeof(struct PageTableEntry) == 4);
static_assert(sizeof(PageDirectory) == PAGE_TABLE_SIZE);
static_assert(sizeof(PageTable) == PAGE_TABLE_SIZE);

PageDirectory *createPageDirectory(MemoryManager *m){
	PageDirectory *pd = allocateAligned(m, sizeof(PageDirectory), PAGE_TABLE_SIZE);
	memset(pd, 0, sizeof(PageDirectory));
	return pd;
}

static PageTable *createPageTable(MemoryManager *m){
	PageTable *pt = allocateAligned(m, sizeof(PageTable), PAGE_TABLE_SIZE);
	memset(pt, 0, sizeof(PageTable));
	return pt;
}

#define SET_ENTRY_ADDRESS(E, A) ((*(unsigned*)(E)) = (((*(unsigned*)(E)) & (4095)) | ((unsigned)(A))))
#define GET_ENTRY_ADDRESS(E) ((void*)((*(unsigned*)(E)) & (~4095)))

void set4KBKernelPage(MemoryManager *m, PageDirectory *pd, unsigned linearAddress, unsigned physicalAddress){
	assert((physicalAddress & 4095) == 0 && (linearAddress & 4095) == 0);
	unsigned i1 = ((linearAddress >> 22) & (PAGE_TABLE_LENGTH - 1));
	unsigned i2 = ((linearAddress >> 12) & (PAGE_TABLE_LENGTH - 1));
	struct PageDirectoryEntry *pde = pd->entry + i1;
	PageTable *pt;
	struct PageTableEntry *pte;
	if(pde->present == 0){
		pt = createPageTable(m);
	}
	else{
		pt = GET_ENTRY_ADDRESS(pde);
	}
	assert((((unsigned)pt) & 4095) == 0);
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
