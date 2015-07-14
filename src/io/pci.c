#include"io.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/handler.h"
#include"interrupt/controller/pic.h"

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

uint32_t readPCIConfig(uint8_t bus, uint8_t dev, uint8_t func, enum PCIConfigRegister reg){
	out32(0xcf8,
		0x80000000 | // enable config cycle
		(bus << 16) | // 7 bits
		(dev << 11) | // 5 bits
		(func << 8) | // 3 bits
		reg // 8 bits
	);
	return in32(0xcfc);
}

typedef struct PCIConfigSpaceList{
	union PCIConfigSpaceLocation{
		struct{
			uint16_t function: 7;
			uint16_t device: 5;
			uint16_t bus: 3;
			uint16_t unused: 1;
		};
		uint16_t value;
	}location;
	struct PCIConfigRegisters{
		uint16_t vendorID, deviceID;
		uint16_t command, status;
		union{
			struct{uint8_t revision, programInterface, subclassCode, classCode;};
			uint32_t classCodes;
		};
		uint8_t cacheLineSize, latencyTimer, headerType, bist;
	}regs;

	const struct PCIConfigSpaceList *next;
}PCIConfigSpaceList;
static_assert(sizeof(struct PCIConfigRegisters) == 16);

static int readConfigSpace(
	uint8_t b, uint8_t d, uint8_t f,
	const PCIConfigSpaceList **head
){
	uint32_t device_vendor = readPCIConfig(b, d, f, DEVICE_VENDOR);
	if((device_vendor & 0xffff) == 0xffff){
		return 0;
	}
	PCIConfigSpaceList *NEW(cs);
	if(cs == NULL){
		printk("cannot allocate memory for PCI configuration space\n");
		return 0;
	}
	cs->location.bus = b;
	cs->location.device = d;
	cs->location.function = f;
	cs->location.unused = 0;
	((uint32_t*)&cs->regs)[0] = device_vendor;
	((uint32_t*)&cs->regs)[1] = readPCIConfig(b, d, f, STATUS_COMMAND);
	((uint32_t*)&cs->regs)[2] = readPCIConfig(b, d, f, CLASS_REVISION);
	((uint32_t*)&cs->regs)[3] = readPCIConfig(b, d, f, HEADER_TYPE);
	cs->next = *head;
	*head = cs;
	return 1;
}

static void deleteConfigSpaceList(const PCIConfigSpaceList **cs){
	const PCIConfigSpaceList *cs1 = (*cs), *cs2;
	while(cs1 != NULL){
		cs2 = cs1->next;
		DELETE((void*)cs1);
		cs1 = cs2;
	}
	*cs = NULL;
}

// return number of found configuration spaces
static int enumeratePCIBridge(uint32_t bus, const PCIConfigSpaceList **csListHead){
	int d;
	int configSpaceCount = 0;
	for(d = 0; d < 32; d++){
		int f, functionCount = 1;
		for(f = 0; f < functionCount; f++){
			if(readConfigSpace(bus, d, f, csListHead) == 0){
				continue;
			}
			configSpaceCount++;
			const PCIConfigSpaceList *cs = *csListHead;
			if(f == 0 && (cs->regs.headerType & 0x80)){
				functionCount = 8;
			}

			// printk("%d.%d.%d: cls=%u,%u,%u dev=%x ven=%x head=%x\n",
			// bus, d, f, cs->regs.classCode, cs->regs.subclassCode, cs->regs.programInterface,
			// cs->regs.deviceID, cs->regs.vendorID, cs->regs.headerType);
			if(cs->regs.classCode == 6 && cs->regs.subclassCode == 4/*PCI bridge*/){
				uint32_t busNumber = readPCIConfig(bus, d, f, BUS_NUMBER);
				if(((busNumber >> 8) & 0xff)/*secondary*/ != ((busNumber >> 16) & 0xff)/*subordinate*/){
					configSpaceCount += enumeratePCIBridge(((busNumber >> 8) & 0xff), csListHead);
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

static int enumerateHostBridge(const PCIConfigSpaceList **csListHead){
	// TODO: lock csListHead if we want to reload driver
	if(*csListHead != NULL){
		printk("reload PCI configuration space");
		deleteConfigSpaceList(csListHead);
	}
	uint32_t classCode = readPCIConfig(0, 0, 0, CLASS_REVISION);
	if(((classCode >> 24) & 0xff)/*class code*/ != 6 || ((classCode >> 16) & 0xff)/*subclass code*/ != 0){
		printk("bus 0, device 0, function 0 is not PCI Host Bridge\n");
		return 0;
	}
	uint32_t headerType = readPCIConfig(0, 0, 0, HEADER_TYPE);
	int f, functionCount = (((headerType >> 16) & 0x80)? 8: 1);
	int configSpaceCount = 0;
	for(f= 0; f < functionCount; f++){
		configSpaceCount += enumeratePCIBridge(f, csListHead);
	}
	return configSpaceCount;
}

const PCIConfigSpaceList **csArray = NULL;
int csArrayLength = 0;

static void enumeratePCIHandler(InterruptParam *p){
	sti();
	int index = (SYSTEM_CALL_ARGUMENT_0(p) & 0xffff);
	if(index >= csArrayLength || index < 0){
		SYSTEM_CALL_RETURN_VALUE_0(p) = 0xffffffff;
		return;
	}
	uint32_t queryClassCode = SYSTEM_CALL_ARGUMENT_1(p);
	uint32_t classMask = SYSTEM_CALL_ARGUMENT_2(p);
	while(index < csArrayLength){
		uint32_t csClassCode = csArray[index]->regs.classCodes;
		if((csClassCode & classMask) == (queryClassCode & classMask)){
			break;
		}
		index++;
	}
	if(index >= csArrayLength){
		SYSTEM_CALL_RETURN_VALUE_0(p) = 0xffffffff;
	}
	else{
		SYSTEM_CALL_RETURN_VALUE_0(p) =
		(index & 0xffff) + (((uint32_t)csArray[index]->location.value) << 16);
	}
}

int systemCall_enumeratePCI(
	uint8_t *bus, uint8_t *dev, uint8_t *func,
	int index, uint32_t classCode, uint32_t classMask
){
	static int pciService = -1;
	while(pciService < 0){
		pciService = systemCall_queryService(PCI_SERVICE_NAME);
		if(pciService >= 0){
			break;
		}
		printk("warning: PCI service has not initalized...\n");
		sleep(20);
	}
	uint32_t indexLow = (index & 0xffff);
	uint32_t r = systemCall4(pciService, &indexLow, &classCode ,&classMask);
	union PCIConfigSpaceLocation location = {value: r >> 16};
	*bus = location.bus;
	*dev = location.device;
	*func = location.function;
	return r & 0xffff;
}

void pciDriver(void){
	const PCIConfigSpaceList *csListHead = NULL, *cs;
	csArrayLength = enumerateHostBridge(&csListHead);
	NEW_ARRAY(csArray, csArrayLength);
	int i;
	cs = csListHead;
	for(i = 0; i < csArrayLength; i++){
		csArray[i] = cs;
		cs = cs->next;
	}
	assert(cs == NULL);
	printk("%d PCI config spaces enumerated\n", csArrayLength);
	registerService(global.syscallTable, PCI_SERVICE_NAME, enumeratePCIHandler, 0);
	while(1){
		hlt();
	}
}
