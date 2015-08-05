#include"io.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"interrupt/systemcall.h"
#include"multiprocessor/processorlocal.h"
#include"resource/resource.h"

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

typedef struct PCIConfigSpace{
	Resource resource;

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
}PCIConfigSpace;
static_assert(sizeof(struct PCIConfigRegisters) == 16);

static int matchPCIClassCodes(Resource *resource, const uintptr_t *arguments){
	PCIConfigSpace *cs = resource->instance;

	if((cs->regs.classCodes & arguments[2]) == (arguments[1] & arguments[2])){
		return 1;
	}
	return 0;
}

static int returnPCILocation(Resource *resource, uintptr_t *returnValues){
	PCIConfigSpace *cs = resource->instance;
	returnValues[0] = cs->location.bus;
	returnValues[1] = cs->location.device;
	returnValues[2] = cs->location.function;
	return 3;
}

static PCIConfigSpace *readAndAddConfigSpace(uint8_t b, uint8_t d, uint8_t f){
	uint32_t device_vendor = readPCIConfig(b, d, f, DEVICE_VENDOR);
	if((device_vendor & 0xffff) == 0xffff){
		return NULL;
	}
	PCIConfigSpace *NEW(cs);
	if(cs == NULL){
		printk("cannot allocate memory for PCI configuration space\n");
		return NULL;
	}
	initResource(&cs->resource, cs, matchPCIClassCodes, returnPCILocation);
	cs->location.bus = b;
	cs->location.device = d;
	cs->location.function = f;
	cs->location.unused = 0;
	((uint32_t*)&cs->regs)[0] = device_vendor;
	((uint32_t*)&cs->regs)[1] = readPCIConfig(b, d, f, STATUS_COMMAND);
	((uint32_t*)&cs->regs)[2] = readPCIConfig(b, d, f, CLASS_REVISION);
	((uint32_t*)&cs->regs)[3] = readPCIConfig(b, d, f, HEADER_TYPE);
	addResource(RESOURCE_PCI_DEVICE, &cs->resource);
	return cs;
}

// return number of found configuration spaces
static int enumeratePCIBridge(uint32_t bus){
	int dev;
	int configSpaceCount = 0;
	for(dev = 0; dev < 32; dev++){
		int func, functionCount = 1;
		for(func = 0; func < functionCount; func++){
			const PCIConfigSpace *cs = readAndAddConfigSpace(bus, dev, func);
			if(cs == NULL){
				continue;
			}
			configSpaceCount++;
			if(func == 0 && (cs->regs.headerType & 0x80)){
				functionCount = 8;
			}

			// printk("%d.%d.%d: cls=%u,%u,%u dev=%x ven=%x head=%x\n",
			// bus, d, f, cs->regs.classCode, cs->regs.subclassCode, cs->regs.programInterface,
			// cs->regs.deviceID, cs->regs.vendorID, cs->regs.headerType);
			if(cs->regs.classCode == 6 && cs->regs.subclassCode == 4/*PCI bridge*/){
				uint32_t busNumber = readPCIConfig(bus, dev, func, BUS_NUMBER);
				if(((busNumber >> 8) & 0xff)/*secondary*/ != ((busNumber >> 16) & 0xff)/*subordinate*/){
					configSpaceCount += enumeratePCIBridge(((busNumber >> 8) & 0xff));
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

static int enumerateHostBridge(void){
	uint32_t classCode = readPCIConfig(0, 0, 0, CLASS_REVISION);
	if(((classCode >> 24) & 0xff)/*class code*/ != 6 || ((classCode >> 16) & 0xff)/*subclass code*/ != 0){
		printk("bus 0, device 0, function 0 is not PCI Host Bridge\n");
		return 0;
	}
	uint32_t headerType = readPCIConfig(0, 0, 0, HEADER_TYPE);
	int f, functionCount = (((headerType >> 16) & 0x80)? 8: 1);
	int configSpaceCount = 0;
	for(f= 0; f < functionCount; f++){
		configSpaceCount += enumeratePCIBridge(f);
	}
	return configSpaceCount;
}

uintptr_t systemCall_discoverPCI(
	uint32_t classCode, uint32_t classMask
){
	uintptr_t type = RESOURCE_PCI_DEVICE;
	return systemCall4(SYSCALL_DISCOVER_RESOURCE, &type, &classCode ,&classMask);
}

void pciDriver(void){
	int pciCount = enumerateHostBridge();
	printk("%d PCI devices enumerated\n", pciCount);
	while(1){
		sleep(1000);
	}
}
