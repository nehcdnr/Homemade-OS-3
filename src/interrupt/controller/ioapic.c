#include"pic.h"
#include"pic_private.h"
#include"interrupt/interrupt.h"
#include"common.h"
#include"memory/memory.h"

typedef struct{
	char signature[4];
	unsigned int length;
	unsigned char revision;
	unsigned char checksum;
	unsigned char oemID[6];
	unsigned char oemTableID[8];
	unsigned int oemRevision;
	unsigned int creatorID;
	unsigned int creatorRevision;
}SDTHeader;

typedef struct{
	SDTHeader header;
	SDTHeader *entry[0];
}RSDT;

// from ACPI spec
typedef struct{
	// ACPI 1.0 (revision = 0)
	char signature[8];
	unsigned char checksum;
	unsigned char oemID[6];
	unsigned char revision;
	RSDT *rsdtAddress;
	// ACPI 2.0 (revision = 2) (unsupported)
	unsigned int length;
	unsigned int xsdtAddress_0_32;
	unsigned int xsdtAddress_32_64;
	unsigned char exChecksum;
	unsigned char reserved[3];
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
	unsigned char length;
}ICSHeader;

typedef struct{
	ICSHeader header;
	unsigned char processorID;
	unsigned char apicID;
	unsigned int flags; // = 1 if enabled
}LocalAPICStruct;

typedef struct{
	ICSHeader header;
	unsigned char ioAPICID;
	unsigned char reserved;
	unsigned *ioAPICAddress;
	unsigned globalSystemInterruptBase;
}IOAPICStruct;

typedef /*__attribute__((__packed__))*/ struct{
	ICSHeader header;
	unsigned char bus;
	unsigned char source;
	unsigned int globalSystemInterrupt;
	unsigned short flags; // bit 2~0: polarity bit 4~2: trigger mode
}SourceOverrideStruct;

typedef struct{
	ICSHeader header;
	unsigned short flags;
	unsigned int globalSystemInterrupt;
}NonMaskableStruct;

typedef struct{
	ICSHeader header;
	unsigned char processorID;
	unsigned short flags;
	unsigned char localAPICLINT;
}LocalNonMaskableStruct;

typedef struct{
	SDTHeader header;
	void* localControllerAddress;
	unsigned int flags; // = 1 if the PC supports 8259 mode
	ICSHeader ics[0]; // interrupt controller structure
}MADT;

static_assert(sizeof(ICSHeader) == 2);
static_assert(sizeof(LocalAPICStruct) == 8);
static_assert(sizeof(IOAPICStruct) == 12);
// gcc always pads to the last element
static_assert(sizeof(SourceOverrideStruct) == 10 + 2);
static_assert(sizeof(NonMaskableStruct) == 8);
static_assert(sizeof(LocalNonMaskableStruct) == 6 + 2);

#define DEFAULT_EBDA_BEGIN (0x9fc00)
#define EBDA_END (0xa0000)
static unsigned findAddressOfEBDA(void){
	unsigned address = ((*(unsigned short*)0x40e)<<4);
	if(address >= EBDA_END || address < 0x80000){
		printf("suspicious EBDA address: %x\n", address);
		address = DEFAULT_EBDA_BEGIN;
	}
	printf("address of EBDA = %x\n", address);
	return address;
}

static unsigned char checksum(const void *data, int size){
	const char *d = (const char*)data;
	unsigned char sum = 0;
	while(size--){
		sum+=*d;
		d++;
	}
	return sum;
}

static void *searchString(const char *string, unsigned beginAddress, unsigned endAddress){
	unsigned a;
	unsigned b = strlen(string);
	for(a = beginAddress; a + b <= endAddress; a++){
		if(strncmp(string, (char*)a, b) == 0)
			return (void*)a;
	}
	return NULL;
}

struct IOAPIC{
	PIC this;

	PIC8259 *legacyPIC;

	int localCount;
	LocalAPICStruct **local;

	int ioCount;
	struct IOAPICProfile{
		IOAPICStruct *ioAPIC;
		int interruptCount;
		InterruptVector *vectorBase;
	}*io;

	int overrideCount;
	SourceOverrideStruct **override;
};

#define IOAPICVER (0x01)
#define IOREDTBL0_32(V) (0x10 + (V)*2 + 0)
#define IOREDTBL32_64(V) (0x10 + (V)*2 + 1)

static void writeIOAPIC(MemoryMappedRegister ioRegSel, unsigned selector, unsigned data){
	MemoryMappedRegister ioWin = (unsigned*)(((unsigned)ioRegSel) + 0x10);
	*ioRegSel = selector;
	*ioWin = data;
}

static unsigned readIOAPIC(MemoryMappedRegister ioRegSel, unsigned selector){
	MemoryMappedRegister ioWin = (unsigned*)(((unsigned)ioRegSel) + 0x10);
	*ioRegSel = selector;
	return *ioWin;
}

static void parseIOAPIC(
	struct IOAPICProfile *profile,
	IOAPICStruct *ias,
	InterruptTable *t
){
	// printf("id = %d, interrupt base = %d, address = %x\n",
	// ias->ioAPICID, ias->globalSystemInterruptBase, ias->ioAPICAddress);
	MemoryMappedRegister address = ias->ioAPICAddress;
	// bit 24~28 = I/O APIC ID
	// printf("%x\n", readIOAPIC(address, 0));
	// bit 0~8 = APIC version, bit 16~24 = redirection entry count - 1
	profile->ioAPIC = ias;
	profile->interruptCount = ((readIOAPIC(address, IOAPICVER) >> 16) & 0xff) + 1;
	profile->vectorBase = registerIRQs(t, ias->globalSystemInterruptBase, profile->interruptCount);
	//printf("%d interrupts registered to IDT\n", profile->interruptCount);
	int v;
	for(v = 0; v < profile->interruptCount; v++){
		unsigned redir0_32 = readIOAPIC(address, IOREDTBL0_32(v));
		//unsigned redir32_64 = readIOAPIC(address, IOREDTBL32_64(v));
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
		writeIOAPIC(address, 0x10 + v * 2 + 0, redir0_32);
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

static void setIOAPICMask(PIC *pic, enum IRQ irq, int setMask){
	IOAPIC *apic = pic->apic;
	int i = irq;
	struct IOAPICProfile *iap = getIOAPICProfile(apic, &i);
	unsigned r = readIOAPIC(iap->ioAPIC->ioAPICAddress, IOREDTBL0_32(i));
	if(setMask)
		r |= 0x00010000;
	else
		r &= (~0x00010000);
	writeIOAPIC(iap->ioAPIC->ioAPICAddress, IOREDTBL0_32(i), r);
}

static InterruptHandler setAPICIRQHandler(PIC *pic, enum IRQ irq, InterruptHandler handler){
	IOAPIC *apic = pic->apic;
	int i = irq;
	struct IOAPICProfile *iap = getIOAPICProfile(apic, &i);
	return setIRQHandler(iap->vectorBase, i, handler);
}

int getProcessorCount(IOAPIC *apic){
	return apic->localCount;
}

static IOAPIC *parseMADT(MemoryManager* m, const MADT *madt, InterruptTable *t){
	printf("MADT total size = %d\n",madt->header.length);
	unsigned offset;
	// pass 1: count tables of different types
	int typeCount[NUMBER_OF_APIC_STRUCT_TYPE];
	int typeIndex;
	for(typeIndex = 0; typeIndex < NUMBER_OF_APIC_STRUCT_TYPE; typeIndex++){
		typeCount[typeIndex] = 0;
	}
	offset = sizeof(MADT);
	while(offset < madt->header.length){
		ICSHeader *icsHeader = (ICSHeader*)(((unsigned)madt) + offset);
		offset+=icsHeader->length;
		if(icsHeader->type < NUMBER_OF_APIC_STRUCT_TYPE)
			typeCount[icsHeader->type]++;
	}
	if(offset != madt->header.length){
		panic("parsing MADT error");
	}
	IOAPIC *apic = allocate(m, sizeof(IOAPIC));
	apic->this.apic = apic;
	apic->localCount = typeCount[LOCAL_APIC];
	apic->ioCount = typeCount[IO_APIC];
	apic->overrideCount = typeCount[SOURCE_OVERRIDE];
	apic->local = allocate(m, sizeof(LocalAPICStruct*) * typeCount[LOCAL_APIC]);
	apic->io = allocate(m, sizeof(struct IOAPICProfile) * typeCount[IO_APIC]);
	apic->override = allocate(m, sizeof(LocalAPICStruct*) * typeCount[SOURCE_OVERRIDE]);

	// pass 2: parse tables
	for(typeIndex = 0; typeIndex < NUMBER_OF_APIC_STRUCT_TYPE; typeIndex++){
		typeCount[typeIndex] = 0;
	}
	offset = sizeof(MADT);
	while(offset < madt->header.length){
		ICSHeader *icsHeader = (ICSHeader*)(((unsigned)madt) + offset);
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
			break;/*
		case NON_MASKABLE:
			{
				NonMaskableStruct *nms = (NonMaskableStruct*)icsHeader;
				printf("nmi = %d\n", nms->globalSystemInterrupt);
			}
			break;
		case LOCAL_NON_MASKABLE:
			{
				LocalNonMaskableStruct *lnms = (LocalNonMaskableStruct*)icsHeader;
				printf("processor = %d, LINT = %d, flags = %d\n",
				lnms->localAPICLINT, lnms->localAPICLINT, lnms->flags);
			}
			break;*/
		default:
			printf("unknown interrupt controller of type %d at offset %d in MADT\n",
			icsHeader->type, offset - icsHeader->length);
			break;
		}
	}
	return apic;
}

static RSDP *searchRSDP(unsigned searchBegin, unsigned searchEnd){
	RSDP *rsdp = NULL;
	while(rsdp == NULL){
		rsdp = searchString("RSD PTR ", searchBegin, searchEnd);
		if(rsdp == NULL)
			break;
		if(checksum((unsigned char*)rsdp, RSDP_REVISION_0_SIZE) != 0){
			searchBegin = 1 + (unsigned)rsdp;
			rsdp = NULL;
		}
	}
	return rsdp;
}

static const RSDT *findRSDT(void){
	const RSDP *rsdp = NULL;
	if(rsdp == NULL)
		rsdp = searchRSDP(findAddressOfEBDA(), EBDA_END);
	if(rsdp == NULL)
		rsdp = searchRSDP(0xe0000, 0x100000);
	if(rsdp == NULL){
		return NULL;
	}
	printf("found Root System Description Pointer at %x\n", rsdp);
	printf("RSDP version = %d\n", rsdp->revision);
	if(rsdp->revision == 2){
		printf("RSDP length = %u, xsdt address = %x:%x\n",
		rsdp->length,
		rsdp->xsdtAddress_32_64, rsdp->xsdtAddress_0_32);
		printf("warning: long mode and ACPI 2.0 are not supported\n");
	}
	const RSDT * rsdt = rsdp->rsdtAddress;
	if(strncmp(rsdt->header.signature, "RSDT", 4) != 0){
		printf("bad RSDT signature\n");
	}
	if(checksum(rsdt, rsdt->header.length) != 0){
		panic("bad RSDT checksum");
	}
	assert((rsdt->header.length - sizeof(RSDT)) % sizeof(SDTHeader*) == 0);
	return rsdt;
}

#define ITERATOR_BEGIN (-1)
#define ITERATOR_END (-2)
static int iterateRSDT(const RSDT *rsdt, int iterator, const char *signature){
	int rsdtEntryCount = (rsdt->header.length - sizeof(RSDT)) / sizeof(SDTHeader*);
	// printf("Root System Description Table length = %d\n", rsdtEntryCount);
	for(iterator++; iterator < rsdtEntryCount; iterator++){
		if(strncmp(rsdt->entry[iterator]->signature, signature, 4) == 0){
			if(checksum(rsdt->entry[iterator], rsdt->entry[iterator]->length) != 0){
				panic("bad checksum");
			}
			return iterator;
		}
	}
	return ITERATOR_END;
}

IOAPIC *initAPIC(MemoryManager* m, InterruptTable *t){
	IOAPIC *apic = NULL;
	const RSDT *rsdt = findRSDT();
	assert(rsdt != NULL);
	int i = ITERATOR_BEGIN;
	while(1){
		i = iterateRSDT(rsdt, i, "APIC"); // MADT signature
		if(i == ITERATOR_END)
			break;
		if(apic != NULL){
			panic("found more than 1 MADTs");
		}
		apic = parseMADT(m, (const MADT*)(rsdt->entry[i]), t);
	}
	setPICMask = setIOAPICMask;
	setPICHandler = setAPICIRQHandler;

	return apic;
}

PIC *castAPIC(IOAPIC *apic){
	return &apic->this;
}

PIC *initPIC(MemoryManager *m, InterruptTable *t){
	if(isAPICSupported() == 0){
		PIC8259 *pic8259 = initPIC8259(m, t);
		return castPIC8259(pic8259);
	}
	// must disable 8259 when enable APIC
	disablePIC8259();
	IOAPIC *ioapic = initAPIC(m, t);
	LAPIC *lapic = initLocalAPIC(m, t, ioapic);
	lapic=(void*)lapic;
	printf("number of processors = %d\n", getProcessorCount(ioapic));
	return castAPIC(ioapic);
}
