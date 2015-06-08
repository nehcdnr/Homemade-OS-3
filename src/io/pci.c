#include"io.h"
#include"assembly/assembly.h"

enum PCIConfigRegister{
	// device id, vendor id
	// 16, 16
	DEVICE_VENDOR = 0x00,
	// status, command
	// 16, 16
	STATUS_COMMAND = 0x04,
	// class, subclass, program interface, revision id
	// 8, 8, 8, 8
	CLASS_REVISION = 0x08,
	// built-in self test, header type, cache line size
	// 8, 8, 8 ,8
	HEADER_TYPE = 0x0c,

	// only for header type = 0x10
	// secondary Latency timer, subordinate bus number, secondary bus number, primary bus number
	// 8, 8, 8, 8
	// primary: direct upstream; secondary: direct downstream; subordinate: most downstream
	BUS_NUMBER = 0x18
};

static uint32_t readConfig(uint8_t bus, uint8_t dev, uint8_t func, enum PCIConfigRegister reg){
	out32(0xcf8,
		0x80000000 | // enable config cycle
		(bus << 16) | // 7 bits
		(dev << 11) | // 5 bits
		(func << 8) | // 3 bits
		reg // 8 bits
	);
	return in32(0xcfc);
}

// vendor ID, device ID, command, status, revision ID, class code, header type

static void enumeratePCIBridge(uint32_t bus){
	printk("enum PCI bus %x\n", bus);
	int  d;
	for(d = 0; d < 32; d++){
		uint32_t device_vendor = readConfig(bus, d, 0, DEVICE_VENDOR);
		if((device_vendor & 0xffff) == 0xffff){ // not exist
			continue;
		}
		uint32_t headerType = readConfig(0, 0, 0, HEADER_TYPE);
		printk("bus %d dev %d headerType: %x\n", bus, d, headerType);
		uint32_t class_revision = readConfig(bus, d, 0, CLASS_REVISION);
		uint32_t cls = ((class_revision >> 24) & 0xff);
		uint32_t subcls = ((class_revision >> 16) & 0xff);
		printk("class %x\n",class_revision);
		if(cls== 0x06 && subcls == 0x04/*PCI bridge*/){
			uint32_t busNumber = readConfig(bus, d, 0, BUS_NUMBER);
			if(((busNumber >> 8) & 0xff)/*secondary*/ != ((busNumber >> 16) & 0xff)/*subordinate*/){
				enumeratePCIBridge(((busNumber >> 8) & 0xff));
			}
		}
	}
}

static void enumeratePCI(void){
	uint32_t device_vendor = readConfig(0, 0, 0, DEVICE_VENDOR);
	if((device_vendor & 0xffff) == 0xffff){ // not exist
		printk("PCI host controller not exist\n");
		return;
	}
	uint32_t headerType = readConfig(0, 0, 0, HEADER_TYPE);
	printk("pci headerType: %x\n",headerType);
	if(headerType & 0x80){ // multiple functions
		int f;
		for(f= 0; f < 8; f++){
			enumeratePCIBridge(f);
		}
	}
	else{
		enumeratePCIBridge(0);
	}
}

void pciDriver(void){
	enumeratePCI();
	while(1){
		hlt();
	}
}
