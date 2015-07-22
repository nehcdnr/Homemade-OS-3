#include <file/file.h>
#include"common.h"
#include"io.h"
#include"memory/memory.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/handler.h"
#include"interrupt/controller/pic.h"
#include"task/task.h"
#include"multiprocessor/spinlock.h"

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

#define HBA_MAX_PORT_COUNT (32)

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
	HBAPortRegister port[HBA_MAX_PORT_COUNT];
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
			uint32_t completeInterrupt: 1; // HBAPortRegister.interruptStatus & (1 << 5)
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

#define DEFAULT_SECTOR_SIZE (512)

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

#define HR_CAPABILITIES_CLO (1 << 24)

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
	sleep(200);
	POLL_UNTIL((pr->commandStatus & (PR_COMMANDSTATUS_CR | PR_COMMANDSTATUS_FR)) == 0, maxTry, ok);
	if(!ok){
		return 0;
	}
	return 1;
}

static int startPort(volatile HBAPortRegister *pr, const volatile HBARegisters *hr){
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
	if(hr->capabilities & HR_CAPABILITIES_CLO){
		pr->commandStatus |= PR_COMMANDSTATUS_CLO;
		POLL_UNTIL((pr->commandStatus & PR_COMMANDSTATUS_CLO) == 0, maxTry, ok);
		if(!ok){
			printk("warning: start AHCI port without clearing command list override bit\n");
		}
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

static int issueIdentifyCommand(volatile HBAPortRegister *pr, HBAPortMemory *pm, PhysicalAddress buffer){
	{
		uint32_t ctbl = pm->commandHeader[0].commandTableBaseLow;
		uint32_t ctbh = pm->commandHeader[0].commandTableBaseHigh;
		struct CommandHeader *pm_ch = &pm->commandHeader[0];
		MEMSET0(pm_ch);
		pm_ch->fisSize = sizeof(HostToDeviceFIS) / 4; // in double words
		pm_ch->write = 0;
		pm_ch->clearBusyOnReceive = 0;
		pm_ch->physicalRegionLength = 1;
		pm_ch->commandTableBaseLow = ctbl;
		pm_ch->commandTableBaseHigh = ctbh;
	}
	{
		struct PhysicalRegion *pm_ct_pr = &pm->commandTable[0].physicalRegion[0];
		MEMSET0(pm_ct_pr);
		pm_ct_pr->dataBaseLow = buffer.value;
		pm_ct_pr->dataBaseHigh = 0;
		pm_ct_pr->byteCount = DEFAULT_SECTOR_SIZE - 1;
		pm_ct_pr->completeInterrupt = 0;
	}
	{
		HostToDeviceFIS *fis = &pm->commandTable[0].hostToDeviceFIS;
		MEMSET0(fis);
		fis->fisType = 0x27;
		fis->command = IDENTIFY_DEVICE;
		fis->updateCommand = 1;
		fis->device = 0;
	}
	//printk("tfd %x, cmd %x, sts %x ci %x is %x sact %x\n",pr->taskFileData,
	//pr->commandStatus, pr->SATAStatus, pr->commandIssue, pr->interruptStatus, pr->SATAActive);
	pr->commandIssue |= (1 << 0);
	int ok;
	// POLL_UNTIL((pr->taskFileData & 0x80)==0, 100000, ok);
	POLL_UNTIL((pr->commandIssue & (1 << 0))==0, 100000, ok);
	if(!ok){
		printk("failed to issue IDENTIFY command\n");
		return 0;
	}
	return 1;
}

static int issueDMACommand(
	volatile HBAPortRegister *pr, HBAPortMemory *pm, uint32_t sectorSize,
	PhysicalAddress buffer, uint64_t lba, unsigned int sectorCount, int write
){
	assert(sectorCount != 0);
	assert(sectorCount <= 65536);
	assert(sectorSize * sectorCount < (1 << 22));
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
		pm_ch->clearBusyOnReceive = 1;
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
		MEMSET0(pm_ct_pr);
		pm_ct_pr->dataBaseLow = buffer.value;
		pm_ct_pr->dataBaseHigh = 0;
		pm_ct_pr->byteCount = sectorCount * sectorSize - 1;
		pm_ct_pr->completeInterrupt = 0;
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
		if(sectorCount == 65536){ // see ATA spec
			fis->sectorCount0_8 = 0;
			fis->sectorCount8_16 = 0;
		}
		else{
			fis->sectorCount0_8 = (sectorCount & 0xff);
			fis->sectorCount8_16 = ((sectorCount >> 8) & 0xff);
		}
		// fis->icc = 0;
		// fis->control = 0;
	}
	//HBAPortRegister *pr
	{
		// issueCommand
		int ok;
		pr->commandIssue |= (1 << 0);
		// when the HBA receives FIS clearing BSY, DRQ, and ERR bit, it clears CI
		POLL_UNTIL((pr->commandIssue & (1 << 0)) == 0, 100000, ok);
		if(!ok){
			printk("failed to issue DMA command\n");
			return 0;
		}
		return 1;
	}
}

static HBAPortMemory *initAHCIPort(volatile HBAPortRegister *pr, const volatile HBARegisters *hr){
	int ok;
	EXPECT((pr->SATAStatus & 0xf0f) == 0x103 /*active and present*/);
	// stop the host controller by clearing ST bit
	ok = stopPort(pr);
	EXPECT(ok);
	// reset pointer to command list and received fis
	HBAPortMemory *pm = allocateKernelPages(sizeof(HBAPortMemory), KERNEL_NON_CACHED_PAGE);
	EXPECT(pm != NULL);
	PhysicalAddress pm_physical = translateExistingPage(kernelPageManager,
			/*TODO: getPageManager(processorLocalTask()), */pm);
	MEMSET0(pm);
	pr->commandBaseHigh = 0;
	pr->commandBaseLow = pm_physical.value + MEMBER_OFFSET(HBAPortMemory, commandHeader[0]);

	pr->fisBaseHigh = 0;
	pr->fisBaseLow = pm_physical.value + MEMBER_OFFSET(HBAPortMemory, receivedFIS);
	// initialize commandList[0]
	pm->commandHeader[0].commandTableBaseHigh = 0;
	pm->commandHeader[0].commandTableBaseLow = pm_physical.value + MEMBER_OFFSET(HBAPortMemory, commandTable[0]);

	ok = startPort(pr, hr);
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


typedef struct AHCIInterruptArgument{
	volatile HBARegisters *hbaRegisters;

	struct DiskDescription{
		uint32_t sectorSize;
		uint64_t sectorCount;
	}desc;

	Spinlock lock;
	struct AHCIPortQueue{
		HBAPortMemory *hbaPortMemory;
		struct DiskRequest *pendingRequest;
		struct DiskRequest *servingRequest;
	}port[HBA_MAX_PORT_COUNT];

	struct AHCIInterruptArgument *next;
}AHCIInterruptArgument;

typedef struct AHCIPortQueue AHCIPortQueue;

static int hasPort(const AHCIInterruptArgument *arg, int p){
	return arg->port[p].hbaPortMemory != NULL;
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
static int initAHCIRegisters(uint32_t bar, AHCIInterruptArgument **argList){
	// enable interrupt
	AHCIInterruptArgument *NEW(arg);
	EXPECT(arg != NULL);
	// baseAddress is aligned to 4K
	PhysicalAddress baseAddress = {bar & 0xfffff000};
	volatile HBARegisters *hba = mapKernelPages(baseAddress, CEIL(sizeof(HBARegisters), PAGE_SIZE), KERNEL_NON_CACHED_PAGE);
	EXPECT(hba != NULL);
	EXPECT(hba->ahciVersion >= 0x10000);
	arg->hbaRegisters = hba;
	arg->lock = initialSpinlock;
	arg->desc.sectorSize = DEFAULT_SECTOR_SIZE;
	//resetAHCI(hba);
	hba->globalHostControl |= ((1<<31)/*AHCI mode*/ | (1 << 1) /*enable interrupt*/);
	//printk("bar5: %x %x\n",bar, hba);
	//printk("command slots: %d\n",(hba->globalHostControl >> 8) & 0x1f);
	//printk("port count: %d\n",(hba->globalHostControl >> 0) & 0x1f);
	//printk("support AHCI only: %d\n", (hba->globalHostControl >> 8) & 1);
	int p;
	uint32_t portImpl = hba->portsImplemented;
	for(p = 0; p < HBA_MAX_PORT_COUNT; p++){
		arg->port[p].hbaPortMemory = NULL;
		arg->port[p].pendingRequest = NULL;
		arg->port[p].servingRequest = NULL;
		if(((portImpl >> p) & 1) == 0){
			continue;
		}
		HBAPortMemory *pm = initAHCIPort(hba->port + p, hba);
		if(pm == NULL){
			// printk("AHCI port %d is unavailable.\n", p);
			continue;
		}
		arg->port[p].hbaPortMemory = pm;
	}
	arg->next = *argList;
	*argList = arg;
	return 1;

	ON_ERROR;
	printk("unsupported AHCI version: %x\n", hba->ahciVersion);
	unmapKernelPages((void*)hba);
	ON_ERROR;
	printk("cannot allocate linear memory for HBA registers\n");
	DELETE(arg);
	ON_ERROR;
	printk("cannot allocate linear memory for HBA interrupt handler\n");
	return 0;
}


// system call
typedef struct DiskRequest DiskRequest;
struct DiskRequest{
	IORequest this;
	Spinlock *lock;
	enum ATACommand command;
	PhysicalAddress buffer;
	uint64_t lba;
	uint32_t sectorCount;
	AHCIInterruptArgument *ahci;
	int portIndex;
	char isWrite;
	char cancellable;
	struct DiskRequest **prev, *next;
};

static int sendDiskRequest(DiskRequest *dr){
	AHCIInterruptArgument *a = dr->ahci;
	switch(dr->command){
	case DMA_READ_EXT:
	case DMA_WRITE_EXT:
		return issueDMACommand(
			&a->hbaRegisters->port[dr->portIndex], a->port[dr->portIndex].hbaPortMemory, a->desc.sectorSize,
			dr->buffer, dr->lba, dr->sectorCount, dr->isWrite
		);
	case IDENTIFY_DEVICE:
		return issueIdentifyCommand(
			&a->hbaRegisters->port[dr->portIndex], a->port[dr->portIndex].hbaPortMemory,
			dr->buffer
		);
	default:
		assert(0); // unknown command;
		return 0;
	}
}

static int destroyDiskRequest(IORequest *ior, __attribute__((__unused__)) uintptr_t *returnValues){
	DELETE(ior->diskRequest);
	return 0;
}

static int cancelDiskRequest(IORequest *ior){
	DiskRequest *dr = ior->diskRequest;
	int cancellable;
	acquireLock(dr->lock);
	cancellable = dr->cancellable;
	if(cancellable){
		REMOVE_FROM_DQUEUE(dr);
	}
	releaseLock(dr->lock);
	return cancellable;
}

static DiskRequest *createDiskRequest(
	HandleIORequest callback, Task *task,
	enum ATACommand cmd,
	PhysicalAddress buffer, uint64_t lba, uint32_t sectorCount,
	AHCIInterruptArgument *a, int portIndex, char isWrite
){
	DiskRequest *NEW(dr);
	EXPECT(dr != NULL);
	initIORequest(&dr->this, dr, callback, task, cancelDiskRequest, destroyDiskRequest);
	dr->lock = &a->lock;
	dr->command = cmd;
	dr->buffer = buffer;
	dr->lba = lba;
	dr->sectorCount = sectorCount;
	dr->ahci = a;
	dr->portIndex = portIndex;
	dr->isWrite = isWrite;
	dr->cancellable = 1;
	dr->prev = NULL;
	dr->next = NULL;
	return dr;
	ON_ERROR;
	return NULL;
}

// disk request queue
static AHCIInterruptArgument **ahciArray = NULL;
static int ahciCount = 0;

// return 0 if failed to issue command
// return 1 if command was issued or no request
static int servePortQueue(AHCIInterruptArgument *a, int portIndex){
	int ok = 1;
	DiskRequest *dr;
	acquireLock(&a->lock);
	assert(a->port[portIndex].servingRequest == NULL);
	dr = a->port[portIndex].pendingRequest;
	if(dr != NULL){
		REMOVE_FROM_DQUEUE(dr);
		dr->cancellable = 0;
		ADD_TO_DQUEUE(dr, &a->port[portIndex].servingRequest);
		ok = sendDiskRequest(dr);
	}
	releaseLock(&a->lock);
	return ok;
}

static void addToPortQueue(DiskRequest *dr, int portIndex){
	AHCIInterruptArgument *a = dr->ahci;
	struct AHCIPortQueue *p = a->port + portIndex;
	acquireLock(&a->lock);
	ADD_TO_DQUEUE(dr, &p->pendingRequest);
	releaseLock(&a->lock);
}

static DiskRequest *removeFromPortQueue(AHCIInterruptArgument *a, int portIndex){
	AHCIPortQueue *p = &a->port[portIndex];
	DiskRequest *dr;
	acquireLock(&a->lock);
	dr = p->servingRequest;
	if(dr != NULL){
		REMOVE_FROM_DQUEUE(dr);
	}
	releaseLock(&a->lock);
	return dr;
}

// interrupt & system call

static void AHCIHandler(InterruptParam *param){
	AHCIInterruptArgument *arg = (AHCIInterruptArgument*)param->argument;
	uint32_t hostStatus = arg->hbaRegisters->interruptStatus;
	// software writes 1 to indicate the interrupt has been handled
	arg->hbaRegisters->interruptStatus = hostStatus;
	int p;
	for(p = 0; p < HBA_MAX_PORT_COUNT; p++){
		if((hostStatus & (1 << p)) == 0)
			continue;
		uint32_t portStatus = arg->hbaRegisters->port[p].interruptStatus;
		arg->hbaRegisters->port[p].interruptStatus = portStatus;
		int i;
		for(i = 0; i < 32; i++){
			if((portStatus & (1 << i)) == 0)
				continue;
		}
		if(portStatus == 0)
			continue;

		DiskRequest *dr = removeFromPortQueue(arg, p);
		if(dr == NULL)
			printk("warning: AHCI driver received unexpected interrupt\n");
		else
			dr->this.handle(&dr->this);

		if(servePortQueue(arg, p) == 0){
			assert(0); // TODO:
			// this.handle(&this); // dr->this.fail()
		}
	}
	processorLocalPIC()->endOfInterrupt(param);
	sti();
	// printk("end of AHCI interrupt\n");
}

// buffer address
static_assert(sizeof(uint32_t) == sizeof(uintptr_t));

#pragma pack(1)

union HBAPortIndex{
	struct{
		uint16_t hbaIndex;
		uint8_t portIndex;
		uint8_t invalid: 1;
		uint8_t identifyCommand: 1;
		uint8_t unused: 6;
	};
	uint32_t value;
}__attribute__((__packed__));

typedef union HBAPortIndex HBAPortIndex;

#pragma pack()

static_assert(sizeof(HBAPortIndex) == sizeof(uint32_t));

static_assert(HBA_MAX_PORT_COUNT < 0x1000);

static int isValidHBAPortIndex(HBAPortIndex index){
	if(/*index.portIndex < 0 || */index.portIndex >= HBA_MAX_PORT_COUNT)
		return 0;
	if(/*index.hbaIndex < 0 || */index.hbaIndex >= ahciCount)
		return 0;
	AHCIInterruptArgument *a = ahciArray[index.hbaIndex];
	if(hasPort(a, index.portIndex) == 0)
		return 0;
	return 1;
}

static HBAPortIndex toDiskCode(int a, int p){
	HBAPortIndex i = {value: 0};
	i.hbaIndex = a;
	i.portIndex = p;
	return i;
}

static void AHCIServiceHandler(InterruptParam *p){
	sti();
	assert(ahciArray == (AHCIInterruptArgument**)p->argument);
	uintptr_t linearBuffer;
	uint64_t lba;
	uint32_t sectorCount;
	HBAPortIndex index;
	int isWrite;
	rwDiskArgument(p, &linearBuffer, &lba, &sectorCount, &index.value, &isWrite);
	PhysicalAddress physicalBuffer = translateExistingPage(
		getTaskPageManager(processorLocalTask()), (void*)linearBuffer
	);
	int ok = isValidHBAPortIndex(index);
	EXPECT(ok);
	DiskRequest *dr = createDiskRequest(resumeTaskByIO, processorLocalTask(),
		(index.identifyCommand? IDENTIFY_DEVICE: (isWrite? DMA_WRITE_EXT: DMA_READ_EXT)),
		physicalBuffer, lba, sectorCount,
		ahciArray[index.hbaIndex], index.portIndex, isWrite
	);
	// improve: multiple physical regions
assert(sectorCount * ahciArray[index.hbaIndex]->desc.sectorSize <= PAGE_SIZE);
	EXPECT(dr != NULL);
	putPendingIO(&dr->this);
	addToPortQueue(dr, index.portIndex);
	if(servePortQueue(ahciArray[index.hbaIndex], index.portIndex) == 0){
		assert(0);
		// TODO: dr->this.handle(&dr->this); // dr->this.fail()
	}

	SYSTEM_CALL_RETURN_VALUE_0(p) = (uint32_t)dr;
	return;
	// destroy(dr);
	ON_ERROR;
	ON_ERROR;
	SYSTEM_CALL_RETURN_VALUE_0(p) = IO_REQUEST_FAILURE;
}

uintptr_t systemCall_rwAHCI(uintptr_t buffer, uint64_t lba, uint32_t sectorCount, uint32_t index, int isWrite){
	static int ahciService = -1;
	while(ahciService < 0){
		ahciService = systemCall_queryService(AHCI_SERVICE_NAME);
		if(ahciService >= 0)
			break;
		printk("warning: AHCI service is not initialized\n");
		sleep(20);
	}
	return systemCall_rwDisk(ahciService,
		buffer, lba, sectorCount,
		index, isWrite
	);
}

static int identifyDisk(int ahciDriver, HBAPortIndex i, struct DiskDescription *d){
	uint16_t *buffer = systemCall_allocateHeap(DEFAULT_SECTOR_SIZE, KERNEL_NON_CACHED_PAGE);
	EXPECT(buffer != NULL);
	memset(buffer, 0, DEFAULT_SECTOR_SIZE);
	i.identifyCommand = 1;
	uintptr_t ior = systemCall_rwDiskSync(ahciDriver, (uintptr_t)buffer, 0, 0, i.value, 0);
	EXPECT(ior != IO_REQUEST_FAILURE);
	// the driver requires 48-bit address
	const uint32_t buffer83 = buffer[83];
	EXPECT(buffer83 & (1 << 10));
	// find sector size
	const uint16_t buffer106 = buffer[106];
	if((buffer106 & ((1 << 14) | (1 << 15))) != (1 << 14)){
		d->sectorSize = DEFAULT_SECTOR_SIZE;
		//printk("disk does not report sector size\n");
	}
	else if((buffer106 & (1 << 12)) == 0){
		d->sectorSize = DEFAULT_SECTOR_SIZE;
		//printk("disk sector size = %u (default)\n", d->sectorSize);
	}
	else{
		d->sectorSize = buffer[117] + (((uint32_t)buffer[118]) << 16);
		//printk("disk sector size = %u\n", d->sectorSize);
	}
	// find number of sectors
#define TO64(W, X, Y, Z) \
(((uint64_t)(W)) << 0) + (((uint64_t)(X)) << 16) + \
(((uint64_t)(Y)) << 32) + (((uint64_t)(Z)) << 48)
	if(buffer[69] & (1<<3)){ // buffer[230] is valid
		d->sectorCount = TO64(buffer[230], buffer[231], buffer[232], buffer[233]);
	}
	else{
		// buffer[60] is always valid
		// if the disk supports 48-bit address, buffer[100] is valid
		const uint32_t buffer60 = buffer[60] + (((uint32_t)buffer[61]) << 16);
		const uint64_t buffer100 = TO64(buffer[100], buffer[101], buffer[102], buffer[103]);
		d->sectorCount = (buffer100 >= buffer60? buffer100: buffer60);
	}
#undef TO64
	//printk("disk sector count = %u\n",(uint32_t)d->sectorCount);
	systemCall_releaseHeap(buffer);
	return 1;
	ON_ERROR;
	ON_ERROR;
	systemCall_releaseHeap(buffer);
	ON_ERROR;
	return 0;
}

static int enumerateAndInitAHCI(AHCIInterruptArgument **ahciList){
	PIC *pic = processorLocalPIC();
	int pciIndex = 0;
	int enumCount = 0;
	while(1){
		uint8_t bus, dev, func;
		// 0x01: mass storage; 0x06: SATA; 01: AHCI >= 1.0
		pciIndex = systemCall_enumeratePCI(&bus, &dev, &func, pciIndex, 0x01060100, 0xffffff00);
		if(pciIndex == 0xffff){
			break;
		}
		pciIndex++;
		uint32_t bar5 = readPCIConfig(bus, dev, func, BASE_ADDRESS_5);
		uint32_t intInfo  = readPCIConfig(bus, dev, func, INTERRUPT_INFORMATION);
		int ok = initAHCIRegisters(bar5, ahciList);

		if(ok == 0){
			continue;
		}
		enumCount++;
		const AHCIInterruptArgument *arg = *ahciList;
		InterruptVector *v = pic->irqToVector(pic, (intInfo & 0xff));
		setHandler(v, AHCIHandler, (uintptr_t)arg);
		pic->setPICMask(pic, (intInfo & 0xff), 0);
	}
	return enumCount;
}

void ahciDriver(void){
	if(ahciArray != NULL || ahciCount != 0){
		panic("cannot initialize AHCI driver");
	}

	ahciCount = 0;
	AHCIInterruptArgument *ahciList = NULL;
	ahciCount = enumerateAndInitAHCI(&ahciList);

	NEW_ARRAY(ahciArray, ahciCount);
	EXPECT(ahciArray != NULL);
	{
		if(ahciArray == NULL){
			panic("cannot initialize AHCI driver");
		}
		int a;
		AHCIInterruptArgument *arg = ahciList;
		for(a = 0; a < ahciCount; a++){
			ahciArray[a] = arg;
			arg = arg->next;
		}
		assert(arg == NULL);
	}
	int ahciDriver = registerService(global.syscallTable, AHCI_SERVICE_NAME, AHCIServiceHandler, (uintptr_t)ahciArray);
	EXPECT(ahciDriver >= 0);

	int a, p;
	for(a = 0; a < ahciCount; a++){
		for(p = 0; p < HBA_MAX_PORT_COUNT; p++){
			if(hasPort(ahciArray[a], p) == 0){
				continue;
			}
			if(identifyDisk(ahciDriver, toDiskCode(a, p), &ahciArray[a]->desc) == 0){
				printk("identify hba %d port %d failed\n", a, p);
				continue;
			}
			readPartitions(AHCI_SERVICE_NAME, ahciDriver, toDiskCode(a, p).value, 0,
				ahciArray[a]->desc.sectorCount, ahciArray[a]->desc.sectorSize);
		}
	}
	printk("found %d AHCI devices\n", ahciCount);
	while(1){
		hlt(); // TODO: create FIFO and wait for interrupt
	}
	ON_ERROR;
	ON_ERROR;
	printk("cannot initialize AHCI driver\n");
	while(1){
		hlt();
	}
}
