#include"common.h"
#include"kernel.h"
#include"memory.h"
#include"memory_private.h"
#include"assembly/assembly.h"
#include"interrupt/handler.h"
#include"interrupt/controller/pic.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"

#define PAGE_TABLE_LENGTH (1024)

typedef struct{
	uint8_t present: 1;
	uint8_t writable: 1;
	uint8_t userAccessible: 1; // CPL = 3
	uint8_t writeThrough: 1;
	uint8_t cacheDisabled: 1;
	uint8_t accessed: 1;
	uint8_t dirty: 1;
	uint8_t zero: 1;
	uint8_t global: 1;
	uint8_t unused: 3; // available for OS; unused
	uint8_t address0_4: 4;
	uint16_t address4_20: 16;
}PageTableEntry;
typedef struct PageTable{
	volatile PageTableEntry entry[PAGE_TABLE_LENGTH];
}PageTable;

#define PAGE_TABLE_REGION_SIZE (PAGE_SIZE * PAGE_TABLE_LENGTH)

#define PAGE_DIRECTORY_LENGTH (1024)

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
	uint8_t unused: 3; // available for OS; unused
	uint8_t address0_4: 4;
	uint16_t address4_20: 16;
}PageDirectoryEntry;
typedef struct PageDirectory{
	volatile PageDirectoryEntry entry[PAGE_DIRECTORY_LENGTH];
}PageDirectory;

static_assert(sizeof(PageDirectoryEntry) == 4);
static_assert(sizeof(PageTableEntry) == 4);
static_assert(sizeof(PageDirectory) % PAGE_SIZE == 0);
static_assert(sizeof(PageTable) % PAGE_SIZE == 0);
static_assert(PAGE_SIZE % MIN_BLOCK_SIZE == 0);

static_assert(USER_LINEAR_BEGIN % PAGE_SIZE == 0);
// see kernel.ld
// static_assert(USER_LINEAR_END % PAGE_SIZE == 0);
// static_assert(KERNEL_LINEAR_BEGIN % PAGE_SIZE == 0);
// static_assert(KERNEL_LINEAR_END % PAGE_SIZE == 0);

#define SET_ENTRY_ADDRESS(E, A) ((*(uint32_t*)(E)) = (((*(uint32_t*)(E)) & (4095)) | ((uint32_t)(A))))
#define GET_ENTRY_ADDRESS(E) ((*(uint32_t*)(E)) & (~4095))
#define GET_ENTRY_FLAGS(E) ((*(uint32_t*)(E)) & 4095)

static PhysicalAddress getPTEAddress(volatile PageTableEntry *e){
	PhysicalAddress a = {GET_ENTRY_ADDRESS(e)};
	return a;
}

static void setPTEAddress(PageTableEntry *e, PhysicalAddress a){
	SET_ENTRY_ADDRESS(e, a.value);
}

static PhysicalAddress getPDEAddress(volatile PageDirectoryEntry *e){
	PhysicalAddress a = {GET_ENTRY_ADDRESS(e)};
	return a;
}

static void setPDEAddress(PageDirectoryEntry *e, PhysicalAddress a){
	SET_ENTRY_ADDRESS(e, a.value);
}

static uint32_t andPTEFlags(volatile PageTableEntry *e, PageAttribute attribute){
	return GET_ENTRY_FLAGS(e) & (uint32_t)attribute;
}

#undef GET_ENTRY_FLAGS
#undef GET_ENTRY_ADDRESS
#undef SET_ENTRY_ADDRESS

static void setCR0PagingBit(void){
	uint32_t cr0 = getCR0();
	if((cr0 & 0x80000000) == 0){
		setCR0(cr0 | 0x80000000);
	}
}

static void invlpgOrSetCR3(uintptr_t linearAddress, size_t size){
	if(size >= 512 * PAGE_SIZE){
		setCR3(getCR3());
	}
	else{
		size_t s;
		for(s = 0; s < size; s += PAGE_SIZE){
			__asm__(
			"invlpg %0\n"
			:
			:"m"(*(char*)(linearAddress + s))
			);
		}
	}
}

// not necessary to invlpg when changing present flag from 0 to 1
#define PD_INDEX(ADDRESS) ((int)(((ADDRESS) >> 22) & (PAGE_DIRECTORY_LENGTH - 1)))
#define PT_INDEX(ADDRESS) ((int)(((ADDRESS) >> 12) & (PAGE_TABLE_LENGTH - 1)))

static void setPDE(
	volatile PageDirectoryEntry *targetPDE, PageAttribute attribute, PhysicalAddress pt_physical
){
	PageDirectoryEntry pde;
	assert((attribute & PRESENT_PAGE_FLAG? 1: 0));
	pde.present = (attribute & PRESENT_PAGE_FLAG? 1: 0);
	pde.writable = (attribute & WRITABLE_PAGE_FLAG? 1: 0);
	pde.userAccessible = (attribute & USER_PAGE_FLAG? 1: 0);
	pde.writeThrough = 0;
	pde.cacheDisabled = (attribute & NON_CACHED_PAGE_FLAG? 1: 0);
	pde.accessed = 0;
	pde.zero1 = 0;
	pde.size4MB = 0;
	pde.zero2 = 0;
	pde.unused = 0;
	setPDEAddress(&pde, pt_physical);
	assert(targetPDE->present == 0);
	(*targetPDE) = pde;
}
/*
static void invalidatePDE(volatile PageDirectoryEntry *targetPDE){
	PageDirectoryEntry pde = *targetPDE;
	pde.present = 0;
	assert(targetPDE->present == 1);
	(*targetPDE) = pde;
}
*/
static void setPTE(
	volatile PageTableEntry *targetPTE, PageAttribute attribute,
	PhysicalAddress physicalAddress
){
	PageTableEntry pte;
	assert((attribute & PRESENT_PAGE_FLAG? 1: 0));
	pte.present = (attribute & PRESENT_PAGE_FLAG? 1: 0);
	pte.writable = (attribute & WRITABLE_PAGE_FLAG? 1: 0);
	pte.userAccessible = (attribute & USER_PAGE_FLAG? 1: 0);
	pte.writeThrough = 0;
	pte.cacheDisabled = (attribute & NON_CACHED_PAGE_FLAG? 1: 0);
	pte.accessed = 0;
	pte.dirty = 0;
	pte.zero = 0;
	pte.global = 0;//(type & GLOBAL_PAGE_FLAG? 1: 0);
	pte.unused = 0;
	setPTEAddress(&pte, physicalAddress);
	(*targetPTE) = pte;
}

static void invalidatePTE(volatile PageTableEntry *targetPTE){
	PageTableEntry pte = *targetPTE;
	// keep the address in page table
	// see unmapPage_LP
	pte.present = 0;
	assert(targetPTE->present == 1);
	(*targetPTE) = pte;
}

static int isPDEPresent(volatile PageDirectoryEntry *e){
	return e->present;
}

static int isPTEPresent(volatile PageTableEntry *e){
	return e->present;
}

// kernel page table

// if external == 1, deleteWhenEmpty has to be 0 and presentCount is ignored
typedef struct{
	uint16_t presentCount;
	uint16_t deleteWhenEmpty: 1;
	uint16_t external: 1;
	uint16_t unused: 14;
}PageTableAttribute;

typedef struct PageTableSet{
	PageDirectory pd;
	//PageTableAttribute ptAttribute[PAGE_DIRECTORY_LENGTH];
	PageTable pt[PAGE_DIRECTORY_LENGTH];
}PageTableSet;

static_assert((sizeof(PageTableAttribute) * PAGE_DIRECTORY_LENGTH) % PAGE_SIZE == 0);

#define NUMBER_OF_PAGE_LOCKS (8)
struct PageManager{
	PhysicalAddress physicalPD;
	uintptr_t reservedBase;
	uintptr_t reservedEnd;
	// pd->entry[i] map to pt[(i + ptIndexBase) % PAGE_DIRECTORY_LENGTH]
	// pdIndexBase = (4G-&pageManager)>>22 so that &pageManager is at pt[0]
	int pdIndexBase;
	PageTableSet *page;
	const PageTableSet *pageInUserSpace;
	Spinlock pdLock[NUMBER_OF_PAGE_LOCKS];
};

PageManager *kernelPageManager = NULL;
// see entry.asm
uint32_t kernelCR3;

uint32_t toCR3(PageManager *p){
	// bit 3: write through
	// bit 4: cache disable
	return p->physicalPD.value;
}


int isKernelLinearAddress(uintptr_t address){
	return address >= KERNEL_LINEAR_BEGIN && address < KERNEL_LINEAR_END;
}

#define PD_INDEX_ADD_BASE(P, LINEAR) ((PD_INDEX(LINEAR) + (P)->pdIndexBase) & (PAGE_DIRECTORY_LENGTH - 1))

static Spinlock *pdLockByLinearAddress(PageManager *p, uintptr_t linear){
	uintptr_t hashValue = PD_INDEX(linear) % NUMBER_OF_PAGE_LOCKS;
	if(linear >= KERNEL_LINEAR_BEGIN && linear < KERNEL_LINEAR_END){
		assert(p == kernelPageManager);
		//return kernelPageManager->pdLock + hashValue;
	}
	return p->pdLock + hashValue;
}

static PageTable *ptByLinearAddress(PageManager *p, uintptr_t linear){
	if(linear >= KERNEL_LINEAR_BEGIN && linear < KERNEL_LINEAR_END){
		// assert(p == kernelPageManager);
		// TODO:
		return kernelPageManager->page->pt + PD_INDEX_ADD_BASE(kernelPageManager, linear);
	}
	return p->page->pt + PD_INDEX_ADD_BASE(p, linear);
}
/*
static PageTableAttribute *linearAddressOfPageTableAttribute(PageManager *p, uintptr_t linear){
	return p->page->ptAttribute + PD_INDEX_ADD_BASE(p, linear);
}
*/
static volatile PageDirectoryEntry *pdeByLinearAddress(const PageManager *p, uintptr_t linear){
	return p->page->pd.entry + PD_INDEX(linear);
}

static volatile PageTableEntry *pteByLinearAddress(PageTable *p, uintptr_t linear){
	return p->entry + PT_INDEX(linear);
}

#undef PD_INDEX_ADD_BASE

// assume the arguments are valid
PhysicalAddress _translatePage(PageManager *p, uintptr_t linearAddress, PageAttribute hasAttribute){
	assert(isPDEPresent(pdeByLinearAddress(p, linearAddress)));
	PageTable *pt = ptByLinearAddress(p, linearAddress);
	volatile PageTableEntry *pte = pteByLinearAddress(pt, linearAddress);
	if(andPTEFlags(pte, hasAttribute) != (uint32_t)hasAttribute){
		PhysicalAddress invalid = {INVALID_PAGE_ADDRESS};
		return invalid;
	}
	return getPTEAddress(pte);
}

// return 1 if success, 0 if error
// if physical == NULL, it is a recursive call to map a PageTable to its belonging PageTableSet.
// In this case, we assume PD is present.
static int setPage(
	PageManager *p,
	PhysicalMemoryBlockManager *physical,
	uintptr_t linearAddress, PhysicalAddress physicalAddress,
	PageAttribute attribute
){
	assert((physicalAddress.value & 4095) == 0 && (linearAddress & 4095) == 0);
	volatile PageDirectoryEntry *pde = pdeByLinearAddress(p, linearAddress);
	Spinlock *lock = pdLockByLinearAddress(p, linearAddress);
	PageTable *pt_linear = ptByLinearAddress(p, linearAddress);
	int pdeOK = 1;
	if(physical != NULL){
		acquireLock(lock);
	}
	//PageTableAttribute *pt_attribute = linearAddressOfPageTableAttribute(p, linearAddress);
	while(isPDEPresent(pde) == 0){
		pdeOK = 0;
		assert(physical != NULL && linearAddress < KERNEL_LINEAR_BEGIN);
		PhysicalAddress pt_physical = {allocatePhysicalBlock(physical, sizeof(PageTable), sizeof(PageTable))};
		if(pt_physical.value == INVALID_PAGE_ADDRESS){
			break;
		}
		//pt_attribute->external = 0;
		//pt_attribute->deleteWhenEmpty = 1;
		//pt_attribute->presentCount = 0;
		// the userAccesible/writable bits in all page levels must be 1 to allow the operations
		setPDE(pde, USER_WRITABLE_PAGE, pt_physical);
		size_t s;
		for(s = 0; s < sizeof(PageTable); s += PAGE_SIZE){
			PhysicalAddress pt_physical_s = {pt_physical.value + s};
			if(setPage(p, NULL, ((uintptr_t)pt_linear) + s, pt_physical_s, KERNEL_PAGE) == 0){
				panic("pt_linear must present in kernel page directory");
			}
		}
		MEMSET0(pt_linear);
		pdeOK = 1;
		break;
	}
	if(physical != NULL){
		releaseLock(lock);
	}
	assert(pdeOK == 0 || pdeOK == 1);
	if(pdeOK == 0){
		return 0;
	}
	assert((((uintptr_t)pt_linear) & 4095) == 0);
	//if(pt_attribute->external == 0){
	//	pt_attribute->presentCount++;
	//}
	// page is protected by linear memory manager, so do not lock PTE
	volatile PageTableEntry *pte = pteByLinearAddress(pt_linear, linearAddress);
	//assert(isPTEPresent(pte) == 0);
	setPTE(pte, attribute, physicalAddress);
	//assert(isPTEPresent(pte) == 1);
	return 1;
}

static void invalidatePage(
	PageManager *p,
	uintptr_t linear
){
	// Spinlock *lock = pdLockByLinearAddress(p, linear);
	// acquireLock(lock);
	assert(isPDEPresent(pdeByLinearAddress(p, linear)));
	int i2 = PT_INDEX(linear);
	PageTable *pt_linear = ptByLinearAddress(p, linear);
	assert(isPTEPresent(pt_linear->entry + i2));
	invalidatePTE(pt_linear->entry + i2);
	// invalidate PDE if the PD is empty
	// releaseLock(lock);
}

static void releaseInvalidatedPage(
	PageManager *p,
	PhysicalMemoryBlockManager *physical,
	uintptr_t linear
){
	PageTable *pt_linear = ptByLinearAddress(p, linear);
	//Spinlock *pdLock = pdLockByLinearAddress(p, linear);
	//PageTableAttribute *pt_attribute = linearAddressOfPageTableAttribute(p ,linear);
#ifndef NDEBUG
	int i2 = PT_INDEX(linear);
	assert(isPDEPresent(pdeByLinearAddress(p, linear)));
	assert(isPTEPresent(pt_linear->entry + i2) == 0);
#endif
	PhysicalAddress page_physical = getPTEAddress(pt_linear->entry + i2);
	releasePhysicalBlock(physical, page_physical.value);
	/* release PageTable and set PD
	acquireLock(pdLock);
	if(pt_attribute->external == 0){
		pt_attribute->presentCount--;
		if(pt_attribute->presentCount == 0 && pt_attribute->deleteWhenEmpty != 0){
			PhysicalAddress pt_physical = getPDEAddress(p->page->pd.entry + i1);
			invalidatePDE(p->page->pd.entry + i1);
			_releasePhysicalPages(physical , pt_physical);
		}
	}
	releaseLock(pdLock);
	*/
}

static size_t evaluateSizeOfPageTableSet(uintptr_t reservedBase, uintptr_t reservedEnd){
	size_t s = 0;
	// PageDirectory
	s += (uintptr_t)&(((PageTableSet*)0)->pt);
	assert(s == PAGE_SIZE * 1);
	// PageTable
	uintptr_t manageBaseIndex =  PD_INDEX(reservedBase);
	uintptr_t manageEndIndex = PD_INDEX(reservedEnd - 1 >= reservedBase? reservedEnd - 1: reservedEnd);
	s += (manageEndIndex - manageBaseIndex + 1) * sizeof(PageTable);
	return s;
}

enum PhyscialMapping{
	// MAP_TO_NEW_PHYSICAL,
	MAP_TO_KERNEL_ALLOCATED = 1,
	MAP_TO_KERNEL_RESERVED = 2
};

static PhysicalAddress linearToPhysical(enum PhyscialMapping mapping, void *linear){
	PhysicalAddress physical;
	switch(mapping){
	case MAP_TO_KERNEL_ALLOCATED:
		physical = _translatePage(kernelPageManager, (uintptr_t)linear, KERNEL_PAGE);
		break;
	case MAP_TO_KERNEL_RESERVED:
		physical.value = (((uintptr_t)(linear)) - KERNEL_LINEAR_BEGIN);
		break;
	default:
		physical.value = INVALID_PAGE_ADDRESS;
	}
	assert(physical.value != INVALID_PAGE_ADDRESS);
	return physical;
}

static void initPageManager(
	PageManager *p, const PageTableSet *tablesLoadAddress, PageTableSet *tables,
	uintptr_t reservedBase, uintptr_t reservedEnd, enum PhyscialMapping mapping
){
	assert(((uintptr_t)tables) % PAGE_SIZE == 0);
	assert(((uintptr_t)tablesLoadAddress) % PAGE_SIZE == 0);

	p->reservedBase = reservedBase;
	p->reservedEnd = reservedEnd;
	p->page = tables;
	p->pageInUserSpace = tablesLoadAddress;
	p->physicalPD = linearToPhysical(mapping, &(tables->pd));
	p->pdIndexBase = ((PAGE_DIRECTORY_LENGTH - PD_INDEX(reservedBase)) & (PAGE_DIRECTORY_LENGTH - 1));
	assert(ptByLinearAddress(p, reservedBase) == tables->pt + 0);
	int i;
	for(i = 0; i < NUMBER_OF_PAGE_LOCKS; i++){
		p->pdLock[i] = initialSpinlock;
	}
	PageDirectory *kpd = &tables->pd;
	MEMSET0(kpd);
}

static void initPageManagerPD(
	PageManager *p,
	uintptr_t linearBase, uintptr_t linearEnd, enum PhyscialMapping mapping
){
	uintptr_t a;
	// use index to avoid overflow
	const uintptr_t b = PD_INDEX(linearBase), e = PD_INDEX(linearEnd - 1);
	for(a = b; a <= e; a++){
		PageTable *kpt = ptByLinearAddress(p, a * PAGE_TABLE_REGION_SIZE);
		//PageTableAttribute *kpta = linearAddressOfPageTableAttribute(p, a * PAGE_TABLE_REGION_SIZE);
		MEMSET0(kpt);
		//kpta->presentCount = 0;
		//kpta->deleteWhenEmpty = 0;
		//kpta->external = 0;
		PhysicalAddress kpt_physical = linearToPhysical(mapping, kpt);
		setPDE(pdeByLinearAddress(p, a * PAGE_TABLE_REGION_SIZE), KERNEL_PAGE, kpt_physical);
	}
}

static void copyPageManagerPD(
	PageManager *dst, const PageManager *src,
	uintptr_t linearBegin, uintptr_t linearEnd
){
	uintptr_t a, b = PD_INDEX(linearBegin), e = PD_INDEX(linearEnd - 1);
	// lock src PD if we want to release PageTable
	for(a = b; a <= e; a++){
		assert(isPDEPresent(dst->page->pd.entry + a) == 0);
		assert(isPDEPresent(src->page->pd.entry + a) != 0);
		//PageTableAttribute *pta = linearAddressOfPageTableAttribute(dst, a * PAGE_TABLE_REGION_SIZE);
		//pta->deleteWhenEmpty = 0;
		//pta->presentCount = 0;
		//pta->external = 1;
		dst->page->pd.entry[a] = src->page->pd.entry[a];
	}
}

static void initPageManagerPT(
	PageManager *p,
	uintptr_t linearBegin, uintptr_t linearEnd, uintptr_t mappedLinearBegin, enum PhyscialMapping mapping
){
	uintptr_t a;
	for(a = linearBegin; a < linearEnd; a += PAGE_SIZE){
		assert(isPDEPresent(pdeByLinearAddress(p, a)));
		PhysicalAddress kp_physical = linearToPhysical(mapping, (void*)(a - linearBegin + mappedLinearBegin));
		// IMPROVE: change to setPTE
		int ok = setPage(p, NULL, a, kp_physical, KERNEL_PAGE);
		assert(ok);
		// reference count should be 2
		ok = addPhysicalBlockReference(kernelLinear->physical, kp_physical.value);
		assert(ok);
	}
}

// see memorymanager.c
PageManager *initKernelPageTable(uintptr_t manageBase, uintptr_t *manageBegin, uintptr_t manageEnd){
	assert(KERNEL_LINEAR_BEGIN % PAGE_TABLE_REGION_SIZE == 0 && KERNEL_LINEAR_END % PAGE_SIZE == 0);
	assert(manageBase >= KERNEL_LINEAR_BEGIN && manageEnd <= KERNEL_LINEAR_END);
	assert(kernelPageManager == NULL);
	uintptr_t newManageBegin = *manageBegin;

	kernelPageManager = (PageManager*)newManageBegin;

	newManageBegin += sizeof(*kernelPageManager);
	newManageBegin = CEIL(newManageBegin, PAGE_SIZE);
	if(newManageBegin + sizeOfPageTableSet > manageEnd){
		panic("insufficient reserved memory for kernel page table");
	}
	initPageManager(
		kernelPageManager, (PageTableSet*)newManageBegin, (PageTableSet*)newManageBegin,
		manageBase, manageEnd, MAP_TO_KERNEL_RESERVED
	);
	initPageManagerPD(kernelPageManager, KERNEL_LINEAR_BEGIN, KERNEL_LINEAR_END, MAP_TO_KERNEL_RESERVED);
	initPageManagerPT(kernelPageManager, manageBase, manageEnd, manageBase, MAP_TO_KERNEL_RESERVED);
	kernelCR3 = toCR3(kernelPageManager);
	setCR3(kernelCR3);
	setCR0PagingBit();
	(*manageBegin) = newManageBegin + sizeOfPageTableSet;
	return kernelPageManager;
}

// multiprocessor TLB

static void sendINVLPG_disabled(
	__attribute__((__unused__)) uint32_t cr3,
	uintptr_t linearAddress, size_t size
){
	invlpgOrSetCR3(linearAddress, size);
}

struct INVLPGArguments{
	volatile uint32_t cr3;
	volatile uintptr_t linearAddress;
	volatile size_t size;
	volatile int isGlobal;
	Barrier barrier;
};
static struct INVLPGArguments args = {0, 0, 0, 0, INITIAL_BARRIER};
static InterruptVector *invlpgVector = NULL;
static void (*sendINVLPG)(uint32_t cr3, uintptr_t linearAddress, size_t size) = sendINVLPG_disabled;

static void invlpgHandler(InterruptParam *p){
	if(args.isGlobal || args.cr3 == getCR3()){
		invlpgOrSetCR3(args.linearAddress, args.size);
	}
	processorLocalPIC()->endOfInterrupt(p);
	addBarrier(&(args.barrier)); // do not wait for the thread generating this interrupt
	sti();
}

static void sendINVLPG_enabled(uint32_t cr3, uintptr_t linearAddress, size_t size){
	static Spinlock lock = INITIAL_SPINLOCK;
	// disabling interrupt during sendINVLPG may result in deadlock
	assert(getEFlags().bit.interrupt == 1);
	acquireLock(&lock);
	{
		PIC *pic = processorLocalPIC();
		args.cr3 = cr3;
		args.linearAddress = linearAddress;
		args.size = size;
		args.isGlobal = (linearAddress >= KERNEL_LINEAR_BEGIN? 1: 0);
		resetBarrier(&(args.barrier));
		pic->interruptAllOther(pic, invlpgVector);
		invlpgOrSetCR3(linearAddress, size);
		addAndWaitAtBarrier(&(args.barrier), pic->numberOfProcessors); // see invlpgHandler
		// assert(args.barrier.count == (unsigned)pic->numberOfProcessors);
	}
	releaseLock(&lock);
}

void initMultiprocessorPaging(InterruptTable *t){
	invlpgVector = registerGeneralInterrupt(t, invlpgHandler, 0);
	sendINVLPG = sendINVLPG_enabled;
}

// assume the linear memory manager has checked the arguments
void _unmapPage(PageManager *p, PhysicalMemoryBlockManager *physical, void *linearAddress, size_t size){
	if(size == 0)
		return;

	size_t s = size;
	do{
		s -= PAGE_SIZE;
		invalidatePage(p, ((uintptr_t)linearAddress) + s);
	}while(s != 0);

	sendINVLPG(p->physicalPD.value, (uintptr_t)linearAddress, size);

	// the pages are not yet released by linear memory manager
	// it is safe to keep address in PTE, and
	// separate invalidatePage & releaseInvalidatedPage
	s = size;
	do{
		s -= PAGE_SIZE;
		releaseInvalidatedPage(p, physical, ((uintptr_t)linearAddress) + s);
	}while(s != 0);

}

// user page table

const size_t sizeOfPageTableSet = sizeof(PageTableSet);
#define MAX_USER_RESERVED_PAGES (4)

// create an page table in kernel linear memory
// with targetBase ~ targetBegin + sizeOfPageTableSet (linear address) mapped to physical address
// the other entries are not accessible in kernel
PageManager *createAndMapUserPageTable(uintptr_t reservedBase, uintptr_t reservedEnd, uintptr_t tablesLoadAddress){
	assert(reservedBase % PAGE_SIZE == 0 && reservedEnd % PAGE_SIZE == 0);
	size_t evalSize = evaluateSizeOfPageTableSet(reservedBase, reservedEnd);
	assert(tablesLoadAddress >= reservedBase && tablesLoadAddress + sizeOfPageTableSet <= reservedEnd);
	assert(evalSize <= MAX_USER_RESERVED_PAGES * PAGE_SIZE && evalSize % PAGE_SIZE == 0);

	PageManager *NEW(p);
	EXPECT(p != NULL);
	PageTableSet *pts = allocateKernelPages(evalSize, KERNEL_PAGE);
	EXPECT(pts != NULL);
	initPageManager(
		p, (PageTableSet*)tablesLoadAddress, pts,
		reservedBase, reservedEnd, MAP_TO_KERNEL_ALLOCATED
	);
	copyPageManagerPD(p, kernelPageManager, KERNEL_LINEAR_BEGIN, KERNEL_LINEAR_END);
	initPageManagerPD(p, tablesLoadAddress, tablesLoadAddress + evalSize, MAP_TO_KERNEL_ALLOCATED);
	initPageManagerPT(p, tablesLoadAddress, tablesLoadAddress + evalSize, (uintptr_t)pts, MAP_TO_KERNEL_ALLOCATED);
	return p;
	// releaseKernelPages(pts);
	ON_ERROR;
	DELETE(p);
	ON_ERROR;
	return NULL;
}

void unmapUserPageTableSet(PageManager *p){
	unmapKernelPages(p->page);
	p->page = (PageTableSet*)p->pageInUserSpace;
}

// assume the page manager remains only reservedBase ~ reservedEnd
// the other pages have been released by releaseAllLinearBlocks
void invalidatePageTable(PageManager *deletePage, PageManager *loadPage){
	assert(getCR3() != toCR3(deletePage) || getEFlags().bit.interrupt == 0);

	PhysicalAddress reservedPhysical[MAX_USER_RESERVED_PAGES];
	size_t evalSize = evaluateSizeOfPageTableSet(deletePage->reservedBase, deletePage->reservedEnd);
	assert(evalSize <= MAX_USER_RESERVED_PAGES * PAGE_SIZE && evalSize % PAGE_SIZE == 0);
	// see initPageManagerPT
	uintptr_t i;
	for(i = 0; i * PAGE_SIZE < evalSize; i++){
		reservedPhysical[i] = _translatePage(deletePage, ((uintptr_t)deletePage->page) + i * PAGE_SIZE, KERNEL_PAGE);
		assert(reservedPhysical[i].value != INVALID_PAGE_ADDRESS);
	}
	{ // see copyPageManagerPD & initPageManagerPD
		int rPDBegin = PD_INDEX(deletePage->reservedBase), rPDEnd = PD_INDEX(deletePage->reservedEnd - 1),
			kPDBegin = PD_INDEX(KERNEL_LINEAR_BEGIN), kPDEnd = PD_INDEX(KERNEL_LINEAR_END - 1), p;
		for(p = 0; p < PAGE_DIRECTORY_LENGTH; p++){
			if((p >= rPDBegin && p <= rPDEnd)){
				p = rPDEnd;
				continue;
			}
			if(p >= kPDBegin && p <= kPDEnd){
				p = kPDEnd;
				continue;
			}
			if(isPDEPresent(deletePage->page->pd.entry + p)){
				releasePhysicalBlock(kernelLinear->physical, getPDEAddress(deletePage->page->pd.entry + p).value);
			}
		}
	}
	if(loadPage != NULL){
		setCR3(toCR3(loadPage));
	}
	for(i = 0; i * PAGE_SIZE < evalSize; i++){
		releasePhysicalBlock(kernelLinear->physical, reservedPhysical[i].value);
	}
}

void releaseInvalidatedPageTable(PageManager *deletePage){
	DELETE(deletePage);
}

void releasePageTable(PageManager *deletePage){
	invalidatePageTable(deletePage, NULL);
	DELETE(deletePage);
}

static int map1Page_LP(
	PageManager *p, PhysicalMemoryBlockManager *physical,
	uintptr_t linearAddress, PhysicalAddress physicalAddress,
	PageAttribute attribute
){
	int ok = addPhysicalBlockReference(physical, physicalAddress.value);
	EXPECT(ok);
	ok = setPage(p, physical, linearAddress, physicalAddress, attribute);
	EXPECT(ok);
	return 1;
	ON_ERROR;
	releasePhysicalBlock(physical, physicalAddress.value);
	ON_ERROR;
	return 0;
}

int _mapPage_L(
	PageManager *p, PhysicalMemoryBlockManager *physical,
	void *linearAddress, size_t size,
	PageAttribute attribute
){
	assert(size % PAGE_SIZE == 0);
	uintptr_t l_addr = (uintptr_t)linearAddress;
	size_t s;
	for(s = 0; s < size; s += PAGE_SIZE){
		PhysicalAddress p_addr = {allocatePhysicalBlock(physical, PAGE_SIZE, PAGE_SIZE)};
		if(p_addr.value == INVALID_PAGE_ADDRESS){
			break;
		}
		int ok = setPage(p, physical, l_addr + s, p_addr, attribute);
		if(ok == 0){
			releasePhysicalBlock(physical, p_addr.value);
			break;
		}
	}
	EXPECT(s >= size);
	return 1;

	ON_ERROR;
	_unmapPage_L(p, physical, linearAddress, s);
	return 0;
}

int _mapContiguousPage_L(
	PageManager *p, PhysicalMemoryBlockManager *physical,
	void *linearAddress, size_t size,
	PageAttribute attribute
){
	assert(size % PAGE_SIZE == 0);
	uintptr_t l_addr = (uintptr_t)linearAddress;
	uintptr_t p_addr0 = allocatePhysicalBlock(physical, size, PAGE_SIZE);
	EXPECT(p_addr0 != INVALID_PAGE_ADDRESS);
	size_t s;
	for(s = 0; s < size; s += PAGE_SIZE){
		PhysicalAddress p_addr = {p_addr0 + s};
		int ok = setPage(p, physical, l_addr + s, p_addr, attribute);
		if(ok == 0)
			break;
	}
	EXPECT(s >= size);
	return 1;
	ON_ERROR;
	size_t a = size;
	while(a > s){
		a -= PAGE_SIZE;
		releasePhysicalBlock(physical, p_addr0 + a);
	}
	_unmapPage_L(p, physical, linearAddress, s);
	ON_ERROR;
	return 0;
}

int _mapPage_LP(
	PageManager *p, PhysicalMemoryBlockManager *physical,
	void *linearAddress, PhysicalAddress physicalAddress, size_t size,
	PageAttribute attribute
){
	size_t s;
	for(s = 0; s < size; s += PAGE_SIZE){
		PhysicalAddress p_addr = {physicalAddress.value + s};
		if(map1Page_LP(p, physical, ((uintptr_t)linearAddress) + s, p_addr, attribute) == 0){
			break;
		}
	}
	EXPECT(s >= size);
	return 1;

	ON_ERROR;
	_unmapPage_LP(p, physical, linearAddress, s);
	return 0;
}
