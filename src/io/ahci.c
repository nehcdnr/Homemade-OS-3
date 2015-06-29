#include"common.h"
#include"io.h"
#include"memory/memory.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/handler.h"
#include"interrupt/controller/pic.h"
#include"task/task.h"

typedef struct{
	uint32_t commandBaseLow/*1KB aligned*/, commandBaseHigh, fisBaseLow/*256 byte aligned*/, fisBaseHigh;
	uint32_t interruptStatus, interruptEnable, commandStatus, reserved0;
	uint32_t taskFileData, signature;
	uint32_t SATAStatus/*SCR0*/, SATAControl/*SCR2*/, SATAError/*SCR1*/, SATAActive/*SCR3*/;
	uint32_t commandIssue;
	// rev. 1.3 registers begin
	uint32_t SATANotification/*SCR4*/, fisSwitchingControl, deviceSleep;
	// rev 1.3 registers end
	uint8_t reserved1[0x70 - 0x48];
	uint8_t verdorSpcific[0x80 - 0x70];
}HBAPortRegister;

static_assert(sizeof(HBAPortRegister) == 0x80);

typedef struct{
	// generic host control
	uint32_t capabilities, globalHostControl, interruptStatus, portsImplemented;
	uint32_t ahciVersion;
	// rev. 1.3 registers begin
	uint32_t completionControl, completionPorts, enclosureLocation;
	uint32_t enclosureControl, extendedCapabilities, handoffControl;
	uint8_t reserved[0x60 - 0x2c];
	uint8_t nvmhci[0xa0 - 0x60];
	// rev 1.3 registers end
	uint8_t vendorSpecific[0x100 - 0xa0];
	HBAPortRegister port[32];
}HBARegisters;

static_assert(sizeof(HBARegisters) == 0x1100);

typedef struct{
	uint8_t fisType;
	uint8_t pmPort: 4;
	uint8_t reserved1: 3;
	uint8_t updateCommand: 1;
	uint8_t command;
	uint8_t feature0_8;
	uint8_t lba0_8, lba8_16, lba16_24, device;
	uint8_t lba24_32, lba32_40, lba40_48, feature8_16;
	uint8_t sectorCount0_8, sectorCount8_16, icc, control;
	uint8_t reserved2[4];
}HostToDeviceFIS;

static_assert(sizeof(HostToDeviceFIS) == 20);

typedef struct{
	// command list
	struct CommandHeader{
		uint16_t fisSize: 5;
		uint16_t atapi: 1;
		uint16_t write: 1;
		uint16_t prefetch: 1;
		uint16_t reset: 1;
		uint16_t bist: 1;
		uint16_t clearBusyOnReceive: 1;
		uint16_t reserved: 1;
		uint16_t portMultiplitierPort: 4;
		uint16_t physicalRegionLength;
		uint32_t transferByteCount;
		uint32_t commandTableBaseLow; // 128 byte aligned; physicalAddress of CommandTable[i]
		uint32_t commandTableBaseHigh;
		uint32_t reserved2[4];
	}commandHeader[32];
	// offset = 1024
	struct ReceivedFIS{
		uint8_t fis[0x100];
	}receivedFIS[6];
	// offset = 1024 + 256 * 6
	// command table
	struct CommandTable{
		union{
			HostToDeviceFIS hostToDeviceFIS;
			uint8_t commandFIS[0x40 - 0x00];
		};
		uint8_t atapiCommand[0x50 - 0x40];
		uint8_t reserved[0x80 - 0x50];
		// offset = 128
		struct PhysicalRegion{
			uint32_t dataBaseLow;
			uint32_t dataBaseHigh;
			uint32_t reserved1;
			uint32_t byteCount: 22;
			uint32_t reserved2: 9;
			uint32_t completeInterrupt: 1;
		}physicalRegion[8]; // length = 0 ~ 65535; size = 0 ~ 0x3fffc
		// size of PhysicalRegion is at least 128
	}commandTable[6];
}HBAPortMemory;

// 6 command slots for a port
// 8 physical regions for a command slot

static_assert(MEMBER_OFFSET(HBAPortMemory, commandHeader) % 1024 == 0);
static_assert(MEMBER_OFFSET(HBAPortMemory, receivedFIS) % 256 == 0);
static_assert(MEMBER_OFFSET(HBAPortMemory, commandTable) % 128 == 0);
static_assert(sizeof(struct CommandHeader) * 32 == 1024);
static_assert(sizeof(struct ReceivedFIS) == 256);
static_assert(sizeof(struct PhysicalRegion) == 16);
static_assert(sizeof(struct CommandTable) == 256);
static_assert(sizeof(HBAPortMemory) % PAGE_SIZE == 0);

#define SECTOR_SIZE (512)

#define POLL_UNTIL(CONDITION, MAX_TRY, SUCCESS) do{\
	int _tryCount;\
	for(_tryCount = 0; 1; _tryCount++){\
		if(_tryCount >= (MAX_TRY)){\
			(SUCCESS) = 0;\
			break;\
		}\
		if(CONDITION){\
			(SUCCESS) = 1;\
			break;\
		}\
		pause();\
	}\
}while(0)

#define PR_COMMANDSTATUS_CR (1 << 15)
#define PR_COMMANDSTATUS_FR (1 << 14)
#define PR_COMMANDSTATUS_FRE (1 << 4)
#define PR_COMMANDSTATUS_CLO (1 << 3)
#define PR_COMMANDSTATUS_ST (1 << 0)

static int stopPort(volatile HBAPortRegister *pr){
	const int maxTry = 10000;
	int ok;
	uint32_t cmd = pr->commandStatus;
	if(cmd & PR_COMMANDSTATUS_ST){
		pr->commandStatus = (cmd ^ PR_COMMANDSTATUS_ST);
	}
	cmd = pr->commandStatus;
	if(cmd & PR_COMMANDSTATUS_FRE){
		pr->commandStatus = (cmd ^ PR_COMMANDSTATUS_FRE);
	}
	sleep(500);
	POLL_UNTIL((pr->commandStatus & (PR_COMMANDSTATUS_CR | PR_COMMANDSTATUS_FR)) == 0, maxTry, ok);
	if(!ok){
		return 0;
	}
	return 1;
}

static int startPort(volatile HBAPortRegister *pr){
	const int maxTry = 10000;
	int ok;
	// command list not running
	POLL_UNTIL((pr->commandStatus & PR_COMMANDSTATUS_CR) == 0, maxTry, ok);
	if(!ok)
		return 0;
	// not busy and not transferring request
	POLL_UNTIL((pr->taskFileData & ((1 << 7) | (1 << 3))) == 0, maxTry, ok);
	if(!ok)
		return 0;
	// physical communication to device is established
	POLL_UNTIL((pr->SATAStatus & 0xf) == 3, maxTry, ok);
	if(!ok)
		return 0;
	// QEMU does not clear CLO bit
	pr->commandStatus |= PR_COMMANDSTATUS_CLO;
	POLL_UNTIL((pr->commandStatus & PR_COMMANDSTATUS_CLO) == 0, maxTry, ok);
	if(!ok){
		printk("warning: start AHCI port without clearing command list override bit\n");
	}

	pr->commandStatus |= PR_COMMANDSTATUS_FRE;// enable receiving DeviceToHostFIS
	pr->commandStatus |= PR_COMMANDSTATUS_ST;// set start bit
	pr->interruptEnable |= 0xffffffff;//TODO
	return 1;
}

enum ATACommand{
	DMA_READ_EXT = 0x25, // DMA read LBA 48
	DMA_WRITE_EXT = 0x35,
	IDENTIFY_DEVICE = 0xec
};

static void identifyAHCI(volatile HBAPortRegister *pr, HBAPortMemory *pm){
	{
		uint32_t ctbl = pm->commandHeader[0].commandTableBaseLow;
		uint32_t ctbh = pm->commandHeader[0].commandTableBaseHigh;
		struct CommandHeader *pm_ch = &pm->commandHeader[0];
		MEMSET0(pm_ch);
		pm_ch->fisSize = sizeof(HostToDeviceFIS) / 4; // in double words
		pm_ch->write = 0;
		pm_ch->clearBusyOnReceive = 0; // TODO: when is busy bit cleared?
		pm_ch->physicalRegionLength = 0;
		pm_ch->commandTableBaseLow = ctbl;
		pm_ch->commandTableBaseHigh = ctbh;
	}
	{
		HostToDeviceFIS *fis = &pm->commandTable[0].hostToDeviceFIS;
		MEMSET0(fis);
		fis->fisType = 0x27;
		fis->command = IDENTIFY_DEVICE;
		fis->updateCommand = 1;
		fis->device = 0;
	}
	printk("tfd %x, cmd %x, sts %x ci %x is %x sact %x\n",pr->taskFileData,
	pr->commandStatus, pr->SATAStatus, pr->commandIssue, pr->interruptStatus, pr->SATAActive);
	pr->commandIssue |= (1 << 0);
	int ok;
	POLL_UNTIL((pr->taskFileData & 0x80)==0, 100000, ok);
	assert(ok);
	printk("IDENTIFY command issued\n");
	/*
	while(1){
		printk("tfd %x, cmd %x, sts %x ci %x is %x sact %x\n",pr->taskFileData,
		pr->commandStatus, pr->SATAStatus, pr->commandIssue, pr->interruptStatus, pr->SATAActive);
		for(rec=0x40;rec<0x54;rec++)printk("%d",pm->receivedFIS[0].fis[rec]);
		printk("\n");
		int aa;
		hlt();
		for(aa=0;aa<100;aa++)hlt();
	}
	*/
}

static void rwAHCI(
	volatile HBAPortRegister *pr, HBAPortMemory *pm,
	PhysicalAddress buffer, uint64_t lba, size_t size, int write
){
	assert(size < (1 << 22));
	// HBAPortMemory *pm
	{
		uint32_t ctbl = pm->commandHeader[0].commandTableBaseLow;
		uint32_t ctbh = pm->commandHeader[0].commandTableBaseHigh;
		struct CommandHeader *pm_ch = &pm->commandHeader[0];
		MEMSET0(pm_ch);
		pm_ch->fisSize = sizeof(HostToDeviceFIS) / 4; // in double words
		//pm_ch->atapi = 0;
		pm_ch->write = (write? 1 : 0);
		//pm_ch->prefetch = 0;
		//pm_ch->reset = 0;
		//pm_ch->bist = 0;
		pm_ch->clearBusyOnReceive = 1; // TODO: when is busy bit cleared?
		//pm->ch->reserved = 0;
		//pm->ch->portMultiplitierPort = 0;
		pm_ch->physicalRegionLength = 1;
		//pm_ch->transferByteCount = 0;
		//pm_ch->commandTableBaseLow;
		//pm_ch->commandTableBaseHigh;
		//pm_ch->reserved2 = 0;
		pm_ch->commandTableBaseLow = ctbl;
		pm_ch->commandTableBaseHigh = ctbh;
	}
	{
		struct PhysicalRegion *pm_ct_pr = &pm->commandTable[0].physicalRegion[0];
		pm_ct_pr->dataBaseLow = buffer.value;
		pm_ct_pr->dataBaseHigh = 0;
		assert(size != 0);
		pm_ct_pr->byteCount = size - 1;
		pm_ct_pr->completeInterrupt = 1;
	}
	{
		HostToDeviceFIS *fis = &pm->commandTable[0].hostToDeviceFIS;
		MEMSET0(fis); // all reserved fields shall be written as 0
		fis->fisType = 0x27;
		// fis->pmPort = 0;
		// fis->reserved1 = 0;
		fis->updateCommand = 1;
		fis->command = (write? DMA_WRITE_EXT: DMA_READ_EXT);
		// fis->feature0_8 = 0;
		fis->lba0_8 = ((lba >> 0) & 0xff);
		fis->lba8_16 = ((lba >> 8) & 0xff);
		fis->lba16_24 = ((lba >> 16) & 0xff);
		fis->device = (1 << 6); // LBA mode
		fis->lba24_32 = ((lba >> 24) & 0xff);
		fis->lba32_40 = ((lba >> 32) & 0xff);
		fis->lba40_48  = ((lba >> 40) & 0xff);
		// fis->feature8_16 = 0;
		size_t sectorCount = DIV_CEIL(size, SECTOR_SIZE);
		assert(sectorCount <= 65536);
		if(sectorCount == 65536) // see ATA spec
			sectorCount = 0;
		fis->sectorCount0_8 = (sectorCount & 0xff);
		fis->sectorCount8_16 = ((sectorCount >> 8) & 0xff);
		// fis->icc = 0;
		// fis->control = 0;
	}
	//HBAPortRegister *pr

	{
		// issueCommand
		pr->commandIssue |= (1 << 0);
		printk("DMA command issued\n");
		/*
		while(1){
			printk("%d %d %x %x %x %x\n", pm->commandHeader[0].transferByteCount,
					pr->SATAStatus, pr->commandIssue, pr->interruptStatus, pr->interruptEnable);
			int aa;
			for(aa=0;aa<100;aa++)hlt();
		}*/
	}
}

static HBAPortMemory *initAHCIPort(volatile HBAPortRegister *pr){
	int ok;
	EXPECT((pr->SATAStatus & 0xf0f) == 0x103 /*active and present*/);
	// stop the host controller by clearing ST bit
	ok = stopPort(pr);
	EXPECT(ok);
	// reset pointer to command list and received fis
	HBAPortMemory *pm = allocateKernelPages(sizeof(HBAPortMemory), KERNEL_NON_CACHED_PAGE);
	EXPECT(pm != NULL);
	PhysicalAddress pm_physical = translateKernelPage(pm);
	MEMSET0(pm);
	pr->commandBaseHigh = 0;
	pr->commandBaseLow = pm_physical.value + MEMBER_OFFSET(HBAPortMemory, commandHeader[0]);

	pr->fisBaseHigh = 0;
	pr->fisBaseLow = pm_physical.value + MEMBER_OFFSET(HBAPortMemory, receivedFIS);
	// initialize commandList[0]
	pm->commandHeader[0].commandTableBaseHigh = 0;
	pm->commandHeader[0].commandTableBaseLow = pm_physical.value + MEMBER_OFFSET(HBAPortMemory, commandTable[0]);

	ok = startPort(pr);
	EXPECT(ok);

	return pm;
	ON_ERROR;
	printk("cannot start AHCI port\n");
	releaseKernelPages(pm);
	ON_ERROR;
	printk("cannot allocate kernel memory for AHCI port");
	ON_ERROR;
	printk("cannot stop AHCI port\n");
	ON_ERROR;
	return NULL;
}

typedef struct{
	volatile HBARegisters *hbaRegisters;
	HBAPortMemory *hbaPortMemory[32];
}AHCIInterruptArgument;

volatile int debug_int;

static void AHCIHandler(InterruptParam *param){
	AHCIInterruptArgument *arg = (AHCIInterruptArgument*)param->argument;
	uint32_t hostStatus = arg->hbaRegisters->interruptStatus;
	int p;
	for(p = 0; p < 32; p++){
		if((hostStatus & (1 << p)) == 0)
			continue;
		uint32_t portStatus = arg->hbaRegisters->port[p].interruptStatus;
		int i;
		for(i = 0; i < 32; i++){
			if((portStatus & (1 << i)) == 0)
				continue;
			printk("port %d, intStatus %d handled\n", p, i);
			debug_int=1;
			arg->hbaRegisters->port[p].interruptStatus |= (1 << i);
		}
		//arg->hbaRegisters->port[p].interruptStatus = 0xffffffff;
	}
	arg->hbaRegisters->interruptStatus = 0xffffffff;
	processorLocalPIC()->endOfInterrupt(param);
	sti();
	printk("end of AHCI interrupt\n");
}
/*
static void resetAHCI(volatile HBARegisters *hba){
	hba->globalHostControl |= (1 << 31);
	hba->globalHostControl |= (1 << 0);
	do{
		printk("hr %x\n",hba->globalHostControl);
		int a;
		for(a = 0;a < 100; a++)hlt();
	}while(hba->globalHostControl & (1 << 0));
}
*/
static void initAHCIRegisters(uint32_t bar, uint32_t intInfo){
	// enable interrupt
	AHCIInterruptArgument *NEW(arg);
	EXPECT(arg != NULL);
	PIC *pic = processorLocalPIC();
	InterruptVector *v = pic->irqToVector(pic, (intInfo & 0xff));
	setHandler(v, AHCIHandler, (uintptr_t)arg);
	pic->setPICMask(pic, (intInfo & 0xff), 0);

	// baseAddress is aligned to 4K
	PhysicalAddress baseAddress = {bar & 0xfffff000};
	volatile HBARegisters *hba = mapKernelPage(baseAddress, CEIL(sizeof(HBARegisters), PAGE_SIZE), KERNEL_NON_CACHED_PAGE);
	EXPECT(hba != NULL);
	EXPECT(hba->ahciVersion >= 0x10000);
	arg->hbaRegisters = hba;
	//resetAHCI(hba);
	hba->globalHostControl |= ((1<<31)/*AHCI mode*/ | (1 << 1) /*enable interrupt*/);
	//printk("bar5: %x %x\n",bar, hba);
	//printk("command slots: %d\n",(hba->globalHostControl >> 8) & 0x1f);
	//printk("port count: %d\n",(hba->globalHostControl >> 0) & 0x1f);
	//printk("support AHCI only: %d\n", (hba->globalHostControl >> 8) & 1);
	int p;
	uint32_t portImpl = hba->portsImplemented;
	for(p = 0; p < 32; p++){
		arg->hbaPortMemory[p] = NULL;
		if(((portImpl >> p) & 1) == 0){
			continue;
		}
		HBAPortMemory *pm = initAHCIPort(hba->port + p);
		if(pm == NULL){
			printk("AHCI port %d is unavailable.\n", p);
			continue;
		}
		arg->hbaPortMemory[p] = pm;
		// testing
#ifndef NDEBUG
		void* buffer = allocateKernelPages(PAGE_SIZE, KERNEL_NON_CACHED_PAGE);
		assert(buffer != NULL);
		PhysicalAddress physicalBuffer = translateExistingPage(
			kernelPageManager/*TODO: getPageManager(processorLocalTask())*/, buffer);
		debug_int = 0;
		identifyAHCI(hba->port + p, pm);
		while(debug_int == 0)hlt();
		debug_int = 0;
		rwAHCI(hba->port + p, pm, physicalBuffer, 0, PAGE_SIZE, 0);
		while(debug_int == 0)hlt();
		//debug_int = 0;
		//rwAHCI(hba->port + p, pm, physicalBuffer, 0, PAGE_SIZE, 0);
		//while(debug_int == 0)hlt();
		printk("AHCI port %d ok\n", p);
		releaseKernelPages(buffer);
#endif
	}
	return;

	ON_ERROR;
	printk("unsupported AHCI version: %x\n", hba->ahciVersion);
	unmapKernelPage((void*)hba);
	ON_ERROR;
	printk("cannot allocate linear memory for HBA registers\n");
	DELETE(arg);
	ON_ERROR;
	printk("cannot allocate linear memory for HBA interrupt handler\n");
}



void ahciDriver(void){
	int index = 0;
	int foundCount = 0;
	while(1){
		uint8_t bus, dev, func;
		// 0x01: mass storage; 0x06: SATA; 01: AHCI >= 1.0
		index = systemCall_enumeratePCI(&bus, &dev, &func, index, 0x01060100, 0xffffff00);
		if(index == 0xffff){
			break;
		}
		index++;
		uint32_t bar5 = readPCIConfig(bus, dev, func, BASE_ADDRESS_5);
		uint32_t intInfo  = readPCIConfig(bus, dev, func, INTERRUPT_INFORMATION);
		initAHCIRegisters(bar5, intInfo);
		foundCount++;
	}
	printk("found %d AHCI devices\n", foundCount);
	while(1){
		hlt(); // TODO: create FIFO and wait for interrupt
	}
}
