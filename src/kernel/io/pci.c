#include"io.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"interrupt/systemcall.h"
#include"file/file.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"

// USB 1.0 (UHCI)
/*
typedef enum{
	USB_COMMAND = 0x00,
	USB_STATUS = 0x02,
	USB_INTERRUPT = 0x04,
	USB_FRAME = 0x06,
	USB_FRAME_BASE = 0x08,
	USB_FRAME_MODIFY = 0x0c,
	USB_PORT_1 = 0x10,
	USB_PORT_2 = 0x12
}UHCIRegister;

static uint16_t readUHCIRegister(uint32_t b, UHCIRegister r){
	return in16(b + r);
}

static void writeUHCIRegister(uint32_t b, UHCIRegister r, uint16_t v){
	out16(b + r, v);
}
*/
// USB 2.0 (EHCI)
/*
struct EHCICapabilityRegisters{
	uint8_t length;
	uint8_t reserved;
	uint16_t version;
	uint32_t structure1;
	uint32_t capability1;
	uint8_t portRoute[8];
};

struct EHCIOperationalRegisters{
	uint32_t command;
	uint32_t status;
	uint32_t interruptEnable;
	uint32_t frameIndex;
	uint32_t segmentSelector4G;
	uint32_t frameListBase;
	uint32_t asyncListAddress;
	uint8_t reserved[36];
	uint32_t configureFlag;
	uint32_t portStatusControl[0];
};

static_assert(sizeof(struct EHCIOperationalRegisters) == 0x44);
*/

// USB 3.0
/*
struct XHCICapabilityRegisters{
	uint32_t structure1;
	uint32_t structure2;
	uint32_t structure3;
	uint32_t capability1;
	uint32_t doorbellOffset;
	uint32_t runtimeRegisterSpaceOffset;
	uint32_t capability2;
};
*/

// PCI
#define PCI_DRIVER_NAME ("pci")

static uint32_t readPCIConfig(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset){
	assert(offset % 4 == 0);
	out32(0xcf8,
		0x80000000 | // enable config cycle
		(bus << 16) | // 7 bits
		(dev << 11) | // 5 bits
		(func << 8) | // 3 bits
		offset // 8 bits
	);
	return in32(0xcfc);
}

typedef struct{
	struct PCIConfigSpace *listHead;
	Spinlock lock;
}PCIManager;

typedef struct PCIConfigSpace{
	struct PCIConfigSpace **prev, *next;

	union PCIConfigSpaceLocation{
		struct{
			uint16_t function: 7;
			uint16_t device: 5;
			uint16_t bus: 3;
			uint16_t unused: 1;
		};
		uint16_t value;
	}location;

	uintptr_t regsSize;
	union{
		PCICommonConfigRegisters regs;
		PCIConfigRegisters0 regs0;
		PCIConfigRegisters1 regs1;
		PCIConfigRegisters2 regs2;
	};
}PCIConfigSpace;

static_assert(MEMBER_OFFSET(PCIConfigSpace, regs.headerType) == MEMBER_OFFSET(PCIConfigSpace, regs0.headerType));
static_assert(MEMBER_OFFSET(PCIConfigSpace, regs.headerType) == MEMBER_OFFSET(PCIConfigSpace, regs1.headerType));
static_assert(MEMBER_OFFSET(PCIConfigSpace, regs.headerType) == MEMBER_OFFSET(PCIConfigSpace, regs2.headerType));
static_assert(sizeof(PCIConfigRegisters0) == 0x40);
static_assert(sizeof(PCIConfigRegisters1) == 0x40);
static_assert(sizeof(PCIConfigRegisters2) == 0x48);

static void addPCIConfigSpace(PCIManager *pciManager, PCIConfigSpace *cs){
	acquireLock(&pciManager->lock);
	ADD_TO_DQUEUE(cs, &pciManager->listHead);
	releaseLock(&pciManager->lock);
}

static void firstPCIConfigSpace(PCIManager *pciManager, const PCIConfigSpace **cs){
	acquireLock(&pciManager->lock);
	*cs = pciManager->listHead;
	releaseLock(&pciManager->lock);
}

static void nextPCIConfigSpace(PCIManager *pciManager, const PCIConfigSpace **cs){
	acquireLock(&pciManager->lock);
	(*cs) = (*cs)->next;
	releaseLock(&pciManager->lock);
}

static PCIConfigSpace *createPCIConfigSpace(uint8_t b, uint8_t d, uint8_t f){
	uint32_t device_vendor = readPCIConfig(b, d, f, 0);
	if((device_vendor & 0xffff) == 0xffff){
		return NULL;
	}
	PCIConfigSpace *NEW(cs);
	if(cs == NULL){
		printk("cannot allocate memory for PCI configuration space\n");
		return NULL;
	}
	cs->prev = NULL;
	cs->next = NULL;
	cs->location.bus = b;
	cs->location.device = d;
	cs->location.function = f;
	cs->location.unused = 0;
	switch(cs->regs.headerType & 0x7f){
	case 0x00:
		cs->regsSize = sizeof(cs->regs0);
		break;
	case 0x01:
		cs->regsSize = sizeof(cs->regs1);
		break;
	case 0x02:
		cs->regsSize = sizeof(cs->regs2);
		break;
	default:
		cs->regsSize = sizeof(cs->regs);
		printk("warning: unrecognized PCI");
		break;
	}
	uint32_t *regs32 = (uint32_t*)&cs->regs;
	regs32[0] = device_vendor;
	uintptr_t i;
	for(i = 1 ; i * sizeof(uint32_t) < cs->regsSize; i ++){
		regs32[i] = readPCIConfig(b, d, f, i * sizeof(uint32_t));
	}
	return cs;
}

// return number of found configuration spaces
static int enumeratePCIBridge(PCIManager *pciManager, uint32_t bus){
	int dev;
	int configSpaceCount = 0;
	for(dev = 0; dev < 32; dev++){
		int func, functionCount = 1;
		for(func = 0; func < functionCount; func++){
			PCIConfigSpace *cs = createPCIConfigSpace(bus, dev, func);
			if(cs == NULL){
				continue;
			}
			addPCIConfigSpace(pciManager, cs);
			configSpaceCount++;
			if(func == 0 && (cs->regs.headerType & 0x80)){
				functionCount = 8;
			}

			// printk("%d.%d.%d: cls=%u,%u,%u dev=%x ven=%x head=%x\n",
			// bus, d, f, cs->regs.classCode, cs->regs.subclassCode, cs->regs.programInterface,
			// cs->regs.deviceID, cs->regs.vendorID, cs->regs.headerType);
			if(cs->regs.classCode == 6 && cs->regs.subclassCode == 4/*PCI bridge*/){
				PCIConfigRegisters1 *regs1 = &cs->regs1;
				if(regs1->secondaryBus != regs1->subordinateBus){
					configSpaceCount += enumeratePCIBridge(pciManager, regs1->secondaryBus);
				}
			}
			// USB
			/*
			if(cs.classCode == 0x0c && cs.subclassCode == 0x03 && (
				cs.programInterface == 0x00 || // UHCI or xHCI
				cs.programInterface == 0x10 || // OHCI
				cs.programInterface == 0x20 || // EHCI
				//cs.programInterface == 0x80 || ?
				//cs.programInterface == 0xfe || // not host controller
				0
			)){
				uint8_t sbrn = (readPCIConfig(bus, d, f, SBRN) & 0xff);
				//if(sbrn == 0x20)
				printk("found usb %d %d %d %d\n",cs.classCode, cs.subclassCode, cs.programInterface, sbrn);
				if(sbrn == 0x10 || sbrn == 0x00){
					uint32_t baseAddress = readPCIConfig(bus, d, f, BASE_ADDRESS_4);
					initUHCIHostController(baseAddress);
				}
				else{
					printk("unsupported USB (SBRN = %x)", sbrn);
				}
			}
			*/
		}
	}
	return configSpaceCount;
}

// TODO: function for reload driver

static int enumerateHostBridge(PCIManager *pciManager){
	uint32_t classCode = readPCIConfig(0, 0, 0, MEMBER_OFFSET(PCICommonConfigRegisters, classCodes));
	if(((classCode >> 24) & 0xff)/*class code*/ != 6 || ((classCode >> 16) & 0xff)/*subclass code*/ != 0){
		printk("bus 0, device 0, function 0 is not PCI Host Bridge\n");
		return 0;
	}
	uint32_t headerType = readPCIConfig(0, 0, 0, MEMBER_OFFSET(PCICommonConfigRegisters, types));
	int f, functionCount = (((headerType >> 16) & 0x80)? 8: 1);
	int configSpaceCount = 0;
	for(f= 0; f < functionCount; f++){
		configSpaceCount += enumeratePCIBridge(pciManager, f);
	}
	return configSpaceCount;
}

uintptr_t enumeratePCI(uint32_t classCode, uint32_t classMask){
	char buf[30];
	snprintf(buf, sizeof(buf), "%s:%x/%x", PCI_DRIVER_NAME, classCode, classMask);
	return syncEnumerateFile(buf);
}

uintptr_t nextPCIConfigRegisters(uintptr_t pciEnumHandle, PCIConfigRegisters *regs, uintptr_t readSize){
	FileEnumeration fe;
	uintptr_t feSize = sizeof(fe);
	uintptr_t r = syncReadFile(pciEnumHandle, &fe, &feSize);
	if(r == IO_REQUEST_FAILURE || feSize != sizeof(fe))
		return 0;
	char buf[20];
	assert(fe.nameLength < 12);
	fe.name[fe.nameLength] = '\0';
	snprintf(buf, sizeof(buf), "%s:%s", PCI_DRIVER_NAME, fe.name);
	uintptr_t pciHandle = syncOpenFile(buf);
	if(pciHandle == IO_REQUEST_FAILURE)
		return 0;
	r = syncSeekReadFile(pciHandle, regs, 0, &readSize);
	if(r == IO_REQUEST_FAILURE)
		readSize = 0;
	r = syncCloseFile(pciHandle);
	assert(r != IO_REQUEST_FAILURE);
	return readSize;
}

static PCIManager pciManager = {NULL, INITIAL_SPINLOCK};

static_assert(sizeof(union PCIConfigSpaceLocation) < sizeof(unsigned));

static int seekReadPCIConfigSpace(
	RWFileRequest *rwfr, OpenedFile *of,
	uint8_t *buffer, uint64_t position, uintptr_t bufferSize
){
	const PCIConfigSpace *cs = getFileInstance(of);
	if(position > sizeof(cs->regs))
		return 0;
	uintptr_t beginPos = (uintptr_t)position;
	uintptr_t endPos = MIN(beginPos + bufferSize, cs->regsSize);
	memcpy(buffer, ((const uint8_t*)&cs->regs) + beginPos, endPos - beginPos);
	completeRWFileIO(rwfr, endPos - beginPos, 0);
	return 1;
}

static void closePCIConfigSpace(CloseFileRequest *cfr, __attribute__((__unused__)) OpenedFile *of){
	completeCloseFile(cfr);
	// do not delete of->instance
}

static int openPCIConfigSpace(OpenFileRequest *ofr, const char *fileName, uintptr_t length,
	__attribute__((__unused__)) OpenFileMode mode){
	unsigned loc;
	int ok = (snscanf(fileName, length, "%x", &loc) == 1);
	EXPECT(ok);

	const PCIConfigSpace *cs;
	firstPCIConfigSpace(&pciManager, &cs);
	while(cs != NULL){
		if(cs->location.value == loc)
			break;
		nextPCIConfigSpace(&pciManager, &cs);
	}
	EXPECT(cs != NULL);
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.seekRead = seekReadPCIConfigSpace;
	ff.close = closePCIConfigSpace;
	completeOpenFile(ofr, (void*)cs, &ff);
	return 1;
	ON_ERROR;
	ON_ERROR;
	return 0;
}

typedef struct PCIEnumerator{
	uint32_t classCode, classMask;
	PCIManager *manager;
	const PCIConfigSpace *cs;
	// add something if we want to remove PCIConfigSpace
	// struct PCIEnumerator **prev, *next;
}PCIEnumerator;

static int readEnumPCI(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize){
	if(bufferSize < sizeof(FileEnumeration)){
		return 0;
	}
	FileEnumeration *fe = (FileEnumeration*)buffer;
	PCIEnumerator *pe = getFileInstance(of);
	uintptr_t readCount = 0;
	while(pe->cs != NULL){
		union PCIConfigSpaceLocation loc = pe->cs->location;
		uint32_t cc = pe->cs->regs.classCodes;
		nextPCIConfigSpace(pe->manager, &pe->cs);
		if((cc & pe->classMask) == (pe->classCode & pe->classMask)){
			fe->nameLength = snprintf(fe->name, sizeof(fe->name)/*MAX_FILE_ENUM_NAME_LENGTH*/, "%x", loc.value);
			readCount = sizeof(FileEnumeration);
			break;
		}
	}
	completeRWFileIO(rwfr, readCount, 0);
	return 1;
}

static void closeEnumPCI(CloseFileRequest *cfr, OpenedFile *of){
	PCIEnumerator *pe = getFileInstance(of);
	completeCloseFile(cfr);
	DELETE(pe);
}

static_assert(sizeof(uint32_t) == sizeof(int));

static int openEnumPCI(OpenFileRequest *ofr, const char *fileName, uintptr_t length, OpenFileMode mode){
	uint32_t classCode, classMask;
	EXPECT(length != 0 && mode.enumeration != 0);
	int ok = (snscanf(fileName, length, "%x/%x", &classCode, &classMask) == 2);
	EXPECT(ok);
	PCIEnumerator *NEW(pe);
	EXPECT(pe);
	assert(pciManager.listHead != NULL);
	pe->manager = &pciManager;
	firstPCIConfigSpace(pe->manager, &pe->cs);
	pe->classCode = classCode;
	pe->classMask = classMask;
	// addFileSystem after initialization. see pciDriver
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.close = closeEnumPCI;
	ff.read = readEnumPCI;
	completeOpenFile(ofr, pe, &ff);
	return 1;
	// DELETE(pe);
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	return 0;
}

static int openPCI(OpenFileRequest *ofr, const char *fileName, uintptr_t length, OpenFileMode mode){
	if(mode.enumeration){
		return openEnumPCI(ofr, fileName, length, mode);
	}
	else{
		return openPCIConfigSpace(ofr, fileName, length, mode);
	}
}

void pciDriver(void){
	int pciCount = enumerateHostBridge(&pciManager);
	printk("%d PCI devices enumerated\n", pciCount);

	FileNameFunctions f = INITIAL_FILE_NAME_FUNCTIONS;
	f.open = openPCI;
	addFileSystem(&f, "pci", strlen("pci"));
	while(1){
		sleep(1000);
	}
}

#ifndef NDEBUG
#include"task/task.h"

void testPCI(void);
void testPCI(void){
	uintptr_t r, enumHandle = enumeratePCI(0x01060100, 0xffffff00);
	//("pci:0/0");
	assert(enumHandle != IO_REQUEST_FAILURE);
	while(1){
		FileEnumeration fe;
		uintptr_t s = sizeof(fe);
		r = syncReadFile(enumHandle, &fe, &s);
		assert(r != IO_REQUEST_FAILURE);
		if(s == 0)
			break;
		uintptr_t i;
		for(i = 0; i < fe.nameLength; i++){
			printk("%c", fe.name[i]);
		}
		printk("\n");

		char buf[30] = "pci:";
		strncpy(buf + strlen(buf), fe.name, fe.nameLength);
		uintptr_t pciHandle = syncOpenFile(buf);
		assert(pciHandle != IO_REQUEST_FAILURE);
		PCICommonConfigRegisters regs;
		s = sizeof(regs);
		r = syncSeekReadFile(pciHandle, &regs, 0, &s);
		assert(r != IO_REQUEST_FAILURE && s == sizeof(regs));
		for(i = 0; i < sizeof(regs) / sizeof(uint32_t); i++){
			printk("%x ",((uint32_t*)&regs)[i]);
		}
		printk("\n");
		r = syncCloseFile(pciHandle);
		assert(r != IO_REQUEST_FAILURE);
	}
	r = syncCloseFile(enumHandle);
	assert(r != IO_REQUEST_FAILURE);
	systemCall_terminate();
}
#endif
