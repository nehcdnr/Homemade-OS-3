#include"io/bios.h"
#include"pic_private.h"
#include"interrupt/interrupt.h"
#include"common.h"
#include"memory/memory.h"

typedef struct{
	int8_t signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	uint8_t oemID[6];
	uint8_t oemTableID[8];
	uint32_t oemRevision;
	uint32_t creatorID;
	uint32_t creatorRevision;
}SDTHeader;

typedef struct{
	SDTHeader header;
	/*SDTHeader **/uint32_t entry[0];
}RSDT;

// from ACPI spec
typedef struct{
	// ACPI 1.0 (revision = 0)
	int8_t signature[8];
	uint8_t checksum;
	uint8_t oemID[6];
	uint8_t revision;
	/* RSDT **/uint32_t rsdtAddress;
	// ACPI 2.0 (revision = 2) (unsupported)
	uint32_t length;
	uint32_t xsdtAddress_0_32;
	uint32_t xsdtAddress_32_64;
	uint8_t exChecksum;
	uint8_t reserved[3];
}RSDP;
#define RSDP_REVISION_0_SIZE (20)

typedef struct{
	enum __attribute__((__packed__)) APICStructureType{
		LOCAL_APIC = 0,
		IO_APIC = 1,
		SOURCE_OVERRIDE = 2,
		NON_MASKABLE = 3,
		LOCAL_NON_MASKABLE = 4,/*,
		LOCAL_ADDRESS_OVERRIDE = 5,
		IO_SAPIC = 6,
		LOCAL_SAPIC = 7,
		PLATFORM_INTERRUPT_SOURCES = 8,
		LOCAL_X2APIC = 9,
		GIC = 10,
		GICD = 11*/
		NUMBER_OF_APIC_STRUCT_TYPE
	}type;
	uint8_t length;
}ICSHeader;

typedef struct{
	ICSHeader header;
	uint8_t processorID;
	uint8_t apicID;
	uint32_t flags; // = 1 if enabled
}LocalAPICStruct;

typedef struct{
	ICSHeader header;
	uint8_t ioAPICID;
	uint8_t reserved;
	uint32_t ioAPICAddress;
	uint32_t globalSystemInterruptBase;
}IOAPICStruct;

typedef struct __attribute__((__packed__)){
	ICSHeader header;
	uint8_t bus;
	uint8_t source;
	uint32_t globalSystemInterrupt;
	uint16_t flags; // bit 2~0: polarity bit 4~2: trigger mode
}SourceOverrideStruct;

typedef struct{
	ICSHeader header;
	uint16_t flags;
	uint32_t globalSystemInterrupt;
}NonMaskableStruct;

typedef struct __attribute__((__packed__)){
	ICSHeader header;
	uint8_t processorID;
	uint8_t flags0_8; // uint16_t flags attribute((__packed__)) fails
	uint8_t flags8_16;
	uint8_t localAPICLINT;
}LocalNonMaskableStruct;

typedef struct{
	SDTHeader header;
	uint32_t localControllerAddress;
	uint32_t flags; // = 1 if the PC supports 8259 mode
	ICSHeader ics[0]; // interrupt controller structure
}MADT;

static_assert(sizeof(ICSHeader) == 2);
static_assert(sizeof(LocalAPICStruct) == 8);
static_assert(sizeof(IOAPICStruct) == 12);
// gcc always pads to the last element
static_assert(sizeof(SourceOverrideStruct) == 10);
static_assert((size_t)(&((SourceOverrideStruct*)0)->flags) == 8);
static_assert(sizeof(NonMaskableStruct) == 8);
static_assert(sizeof(LocalNonMaskableStruct) == 6);
static_assert((size_t)(&((LocalNonMaskableStruct*)0)->localAPICLINT) == 5);

static_assert(sizeof(enum APICStructureType) == 1);

struct IOAPIC{

	int localCount;
	LocalAPICStruct **local;

	int ioCount;
	struct IOAPICProfile{
		IOAPICStruct *ioAPIC;
		int interruptCount;
		MemoryMappedRegister mappedRegister;
		InterruptVector *vectorBase;
	}*io;

	int overrideCount;
	SourceOverrideStruct **override;
};

#define IOAPICVER (0x01)
#define IOREDTBL0_32(V) (0x10 + (V)*2 + 0)
#define IOREDTBL32_64(V) (0x10 + (V)*2 + 1)

static void writeIOAPIC(MemoryMappedRegister ioRegSel, uint32_t selector, uint32_t data){
	MemoryMappedRegister ioWin = (MemoryMappedRegister)(((uintptr_t)ioRegSel) + 0x10);
	*ioRegSel = selector;
	*ioWin = data;
}

static uint32_t readIOAPIC(MemoryMappedRegister ioRegSel, uint32_t selector){
	MemoryMappedRegister ioWin = (MemoryMappedRegister)(((uintptr_t)ioRegSel) + 0x10);
	*ioRegSel = selector;
	return *ioWin;
}

#define IOAPIC_MAPPING_SIZE (PAGE_SIZE)

static void parseIOAPIC(
	struct IOAPICProfile *profile,
	IOAPICStruct *ias,
	InterruptTable *t
){
	printk("IOAPIC id = %d, interrupt base = %d, address = %x\n",
	ias->ioAPICID, ias->globalSystemInterruptBase, ias->ioAPICAddress);
	PhysicalAddress ioapicPhysical = {FLOOR(ias->ioAPICAddress, IOAPIC_MAPPING_SIZE)};
	void *ioapicMappedBegin = mapKernelPage(ioapicPhysical, PAGE_SIZE);
	(*(volatile uint32_t**)&profile->mappedRegister) =
	(MemoryMappedRegister)(((uintptr_t)ioapicMappedBegin) + ias->ioAPICAddress % PAGE_SIZE);
	// bit 24~28 = I/O APIC ID
	// printf("%x\n", readIOAPIC(address, 0));
	// bit 0~8 = APIC version, bit 16~24 = redirection entry count - 1
	profile->ioAPIC = ias;
	profile->interruptCount = ((readIOAPIC(profile->mappedRegister, IOAPICVER) >> 16) & 0xff) + 1;
	profile->vectorBase = registerIRQs(t, ias->globalSystemInterruptBase, profile->interruptCount);
	//printf("%d interrupts registered to IDT\n", profile->interruptCount);
	int v;
	for(v = 0; v < profile->interruptCount; v++){
		uint32_t redir0_32 = readIOAPIC(profile->mappedRegister, IOREDTBL0_32(v));
		//uint32_t redir32_64 = readIOAPIC(address, IOREDTBL32_64(v));
		//printf(" %x %x/", redir0_32, redir32_64);
		/*
		bit 64~56: destination (in physical or logical mode)
		bit 16: mask
		bit 15: level trigger = 1, edge trigger = 0
		bit 14: remote IRR (in level trigger mode)
		bit 13: polarity
		bit 12: pending = 1
		bit 11: physical destination = 0, logical destination = 1
		bit 10~8: delivery mode
			fixed = 000
			lowest priority = 001
			SMI = 010
			NMI = 100
			INIT = 101
			ExtINT = 111
		bit 7~0: vector
		*/
		redir0_32 = ((redir0_32 & ~(0x000100ff)) | 0x00010000 | (toChar(profile->vectorBase) + v));
		writeIOAPIC(profile->mappedRegister, 0x10 + v * 2 + 0, redir0_32);
	}
}

static struct IOAPICProfile *getIOAPICProfile(IOAPIC *apic, int *irq){
	int i;
	for(i = 0; i < apic->overrideCount; i++){
		if(apic->override[i]->source == *irq){
			*irq = apic->override[i]->globalSystemInterrupt;
			break;
		}
	}
	for(i = 0; i < apic->ioCount; i++){
		IOAPICStruct *ias = apic->io[i].ioAPIC;
		int b = ias->globalSystemInterruptBase;
		int c = apic->io[i].interruptCount;
		if(*irq < b + c && *irq >= b)
			return apic->io + i;
	}
	panic("cannot find I/O APIC structure by IRQ");
	return NULL;
}

void apic_setPICMask(PIC *pic, enum IRQ irq, int setMask){
	IOAPIC *apic = pic->apic->ioapic;
	int i = irq;
	struct IOAPICProfile *iap = getIOAPICProfile(apic, &i);
	uint32_t r = readIOAPIC(iap->mappedRegister, IOREDTBL0_32(i));
	if(setMask)
		r |= 0x00010000;
	else
		r &= (~0x00010000);
	writeIOAPIC(iap->mappedRegister, IOREDTBL0_32(i), r);
}

InterruptVector *apic_irqToVector(PIC *pic, enum IRQ irq){
	IOAPIC *apic = pic->apic->ioapic;
	int i = irq;
	struct IOAPICProfile *iap = getIOAPICProfile(apic, &i);
	return getVector(iap->vectorBase, i);
}

int getNumberOfLAPIC(IOAPIC *apic){
	return apic->localCount;
}

uint32_t getLAPICIDByIndex(IOAPIC *ioapic, int index){
	return ioapic->local[index]->apicID;
}

static IOAPIC *parseMADT(const MADT *madt, InterruptTable *t){
	printk("MADT total size = %d\n",madt->header.length);
	size_t offset;
	// pass 1: count tables of different types
	int typeCount[NUMBER_OF_APIC_STRUCT_TYPE];
	int typeIndex;
	for(typeIndex = 0; typeIndex < NUMBER_OF_APIC_STRUCT_TYPE; typeIndex++){
		typeCount[typeIndex] = 0;
	}
	offset = sizeof(MADT);
	while(offset < madt->header.length){
		ICSHeader *icsHeader = (ICSHeader*)(((uintptr_t)madt) + offset);
		offset += icsHeader->length;
		if(icsHeader->type < NUMBER_OF_APIC_STRUCT_TYPE)
			typeCount[icsHeader->type]++;
	}
	if(offset != madt->header.length){
		panic("parsing MADT error");
	}
	IOAPIC *NEW(apic);
	apic->localCount = typeCount[LOCAL_APIC];
	apic->ioCount = typeCount[IO_APIC];
	apic->overrideCount = typeCount[SOURCE_OVERRIDE];
	NEW_ARRAY(apic->local, typeCount[LOCAL_APIC]);
	NEW_ARRAY(apic->io, typeCount[IO_APIC]);
	NEW_ARRAY(apic->override, typeCount[SOURCE_OVERRIDE]);

	// pass 2: parse tables
	for(typeIndex = 0; typeIndex < NUMBER_OF_APIC_STRUCT_TYPE; typeIndex++){
		typeCount[typeIndex] = 0;
	}
	offset = sizeof(MADT);
	while(offset < madt->header.length){
		ICSHeader *icsHeader = (ICSHeader*)(((uintptr_t)madt) + offset);
		offset += icsHeader->length;
		const enum APICStructureType type = icsHeader->type;
		if(type >= NUMBER_OF_APIC_STRUCT_TYPE){
			continue;
		}
		const int cnt = typeCount[type];
		typeCount[type]++;
		switch(type){
		case LOCAL_APIC:
			apic->local[cnt] = (LocalAPICStruct*)icsHeader;
			break;
		case IO_APIC:
			parseIOAPIC(apic->io + cnt, (IOAPICStruct*)icsHeader, t);
			break;
		case SOURCE_OVERRIDE:
			apic->override[cnt] = (SourceOverrideStruct*)icsHeader;
			break;
		case NON_MASKABLE:
			/*{
				NonMaskableStruct *nms = (NonMaskableStruct*)icsHeader;
				printf("nmi = %d\n", nms->globalSystemInterrupt);
			}*/
			break;
		case LOCAL_NON_MASKABLE:
			/*{
				LocalNonMaskableStruct *lnms = (LocalNonMaskableStruct*)icsHeader;
				printf("processor = %d, LINT = %d, flags = %d\n",
				lnms->localAPICLINT, lnms->localAPICLINT, lnms->flags);
			}*/
			break;
		default:
			printk("unknown interrupt controller of type %d at offset %d in MADT\n",
			icsHeader->type, offset - icsHeader->length);
			break;
		}
	}
	return apic;
}

static uintptr_t findRSDT(void){
	const RSDP *rsdp = NULL;
	if(rsdp == NULL)
		rsdp = searchStructure(RSDP_REVISION_0_SIZE, "RSD PTR ",
			findAddressOfEBDA(KERNEL_LINEAR_BEGIN), KERNEL_LINEAR_BEGIN + EBDA_END);
	if(rsdp == NULL)
		rsdp = searchStructure(RSDP_REVISION_0_SIZE, "RSD PTR ",
			KERNEL_LINEAR_BEGIN + 0xe0000, KERNEL_LINEAR_BEGIN + 0x100000);
	if(rsdp == NULL){
		return UINTPTR_NULL;
	}
	printk("found Root System Description Pointer at %x\n", rsdp);
	printk("RSDP version = %d\n", rsdp->revision);
	if(rsdp->revision == 2){
		printk("RSDP length = %u, xsdt address = %x:%x\n",
		rsdp->length,
		rsdp->xsdtAddress_32_64, rsdp->xsdtAddress_0_32);
		printk("warning: long mode and ACPI 2.0 are not supported\n");
	}
	return rsdp->rsdtAddress;
}

static RSDT *mapRSDT(const uintptr_t rsdtPhysical){
	RSDT *rsdt;
	PhysicalAddress rsdtPageBegin = {FLOOR(rsdtPhysical, PAGE_SIZE)};

	void *rsdtMappedBegin = mapKernelPage(rsdtPageBegin, PAGE_SIZE * 2);
	if(rsdtMappedBegin == NULL){
		panic("fail allocating memory");
	}
	rsdt = (RSDT*)(((uintptr_t)rsdtMappedBegin) + rsdtPhysical % PAGE_SIZE);
	size_t rsdtMappedSize= CEIL(rsdtPhysical % PAGE_SIZE + rsdt->header.length, PAGE_SIZE);
	unmapKernelPage(rsdtMappedBegin);

	rsdtMappedBegin = mapKernelPage(rsdtPageBegin, rsdtMappedSize);
	if(rsdtMappedBegin == NULL){
		panic("fail allocating memory");
	}
	rsdt = (RSDT*)(((uintptr_t)rsdtMappedBegin) + rsdtPhysical % PAGE_SIZE);
	return rsdt;
}

IOAPIC *initAPIC(InterruptTable *t){
	uintptr_t rsdtPhysical = findRSDT();
	const RSDT *rsdt = mapRSDT(rsdtPhysical);
	if(strncmp(rsdt->header.signature, "RSDT", 4) != 0){
		printk("warning: bad RSDT signature\n");
	}
	if(checksum(rsdt, rsdt->header.length) != 0){
		panic("bad RSDT checksum");
	}

	assert(rsdt != NULL);
	int rsdtEntryCount = (rsdt->header.length - sizeof(*rsdt)) / sizeof(rsdt->entry[0]);
	IOAPIC *apic = NULL;
	int i;
	for(i = 0; i < rsdtEntryCount; i++){
		const uintptr_t madtPhysical = rsdt->entry[i];
		PhysicalAddress madtPageBegin = {FLOOR(madtPhysical, PAGE_SIZE)};
		void *madtMappedBegin = mapKernelPage(madtPageBegin, PAGE_SIZE * 2);
		SDTHeader *madt = (SDTHeader*)(((uintptr_t)madtMappedBegin) + madtPhysical % PAGE_SIZE);
		size_t madtMappedSize = CEIL(madtPhysical % PAGE_SIZE + madt->length, PAGE_SIZE);
		int signatureOK = (strncmp(madt->signature, "APIC", 4) == 0);
		unmapKernelPage(madtMappedBegin);
		if(signatureOK == 0){
			continue;
		}
		madtMappedBegin = mapKernelPage(madtPageBegin, madtMappedSize);
		madt = (SDTHeader*)(((uintptr_t)madtMappedBegin) + madtPhysical % PAGE_SIZE);
		if(checksum(madt, madt->length) != 0){
			panic("bad MADT checksum");
		}
		if(apic != NULL){
			printk("warning: found more than 1 MADTs");
			continue;
		}
		apic = parseMADT((const MADT*)(madt), t);
	}
	return apic;
}
