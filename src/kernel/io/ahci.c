
#include"common.h"
#include"io.h"
#include"memory/memory.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/handler.h"
#include"file/file.h"
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

static_assert(PAGE_SIZE % DEFAULT_SECTOR_SIZE == 0);

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
	pr->interruptEnable |= 0xffffffff;//IMPROVE
	return 1;
}

enum ATACommand{
	DMA_READ_EXT = 0x25, // DMA read LBA 48
	DMA_WRITE_EXT = 0x35,
	IDENTIFY_DEVICE = 0xec
};

static int issueIdentifyCommand(volatile HBAPortRegister *pr, HBAPortMemory *pm, uintptr_t buffer){
	if(buffer % DEFAULT_SECTOR_SIZE != 0){
		return 0;
	}
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
		pm_ct_pr->dataBaseLow = buffer;
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
	uintptr_t buffer, uint64_t lba, unsigned int sectorCount, int write
){
	if(buffer % sectorSize != 0 ||
	sectorCount == 0 || sectorCount > 65536 ||
	sectorSize * sectorCount >= (1 << 22)){
		return 0;
	}
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
		pm_ct_pr->dataBaseLow = buffer;
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
	HBAPortMemory *pm = allocateKernelPages(CEIL(sizeof(HBAPortMemory), PAGE_SIZE), KERNEL_NON_CACHED_PAGE);
	EXPECT(pm != NULL);
	PhysicalAddress pm_physical = checkAndTranslatePage(kernelLinear, pm);
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
	checkAndReleaseKernelPages(pm);
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
		uintptr_t sectorSize;
		uint64_t sectorCount;
	}desc;

	Spinlock lock;
	struct AHCIPortQueue{
		HBAPortMemory *hbaPortMemory;
		struct DiskRequest *pendingRequest;
		struct DiskRequest *servingRequest;
	}port[HBA_MAX_PORT_COUNT];

	// manager
	struct AHCIInterruptArgument *next;
	uint16_t hbaIndex;
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
static AHCIInterruptArgument *initAHCIRegisters(uint32_t bar){
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
	//arg->desc.sectorCount = 1;
	//arg->hbaIndex = 0xffff;
	//arg->next = NULL;
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

	return arg;

	ON_ERROR;
	printk("unsupported AHCI version: %x\n", hba->ahciVersion);
	unmapKernelPages((void*)hba);
	ON_ERROR;
	printk("cannot allocate linear memory for HBA registers\n");
	DELETE(arg);
	ON_ERROR;
	printk("cannot allocate linear memory for HBA interrupt handler\n");
	return NULL;
}

// system call
typedef struct DiskRequest{
	// when sending a READ or WRITE command, ior == NULL and rwfr != NULL
	RWFileRequest *rwfr;
	// when sending a IDENTIFY command, ior != NULL and rwfr == NULL
	IORequest *ior;
	Spinlock *lock;
	enum ATACommand command;

	LinearMemoryManager *physicalBufferManager;
	// not aligned
	void *inputBuffer;
	uintptr_t inputSize;
	// aligned to page
	PhysicalAddress physicalBufferPage;
	// if inputBuffer is aligned, sectorBuffer == inputBuffer and not need to copy
	// aligned to Page
	void *sectorBufferPage;
	// aligned to sector;
	uintptr_t sectorBufferOffset;
	// not aligned
	uintptr_t bufferOffset;

	uint64_t lba;
	uint32_t sectorCount;
	AHCIInterruptArgument *ahci;
	int portIndex;
	char isWrite;
	struct DiskRequest **prev, *next;
}DiskRequest;


static uintptr_t diskPhysicalSectorBuffer(DiskRequest *dr){
	return dr->physicalBufferPage.value + dr->sectorBufferOffset;
}

static void *diskLinearBuffer(DiskRequest *dr){
	return (void*)(((uintptr_t)dr->sectorBufferPage) + dr->bufferOffset);
}

static int hasSeparateSectorBuffer(DiskRequest *dr){
	return ((uintptr_t)dr->inputBuffer) != ((uintptr_t)dr->sectorBufferPage) + dr->bufferOffset;
}

static int sendDiskRequest(DiskRequest *dr){
	AHCIInterruptArgument *a = dr->ahci;
	switch(dr->command){
	case DMA_READ_EXT:
	case DMA_WRITE_EXT:
		return issueDMACommand(
			&a->hbaRegisters->port[dr->portIndex], a->port[dr->portIndex].hbaPortMemory, a->desc.sectorSize,
			diskPhysicalSectorBuffer(dr), dr->lba, dr->sectorCount, dr->isWrite
		);
	case IDENTIFY_DEVICE:
		return issueIdentifyCommand(
			&a->hbaRegisters->port[dr->portIndex], a->port[dr->portIndex].hbaPortMemory,
			diskPhysicalSectorBuffer(dr)
		);
	default:
		assert(0); // unknown command;
		return 0;
	}
}
/*
static void cancelRWAHCI(void *instance){
	DiskRequest *dr = instance;
	acquireLock(dr->lock);
	REMOVE_FROM_DQUEUE(dr);
	releaseLock(dr->lock);
	releaseReservedPage(dr->memoryManager, dr->buffer);
	DELETE(dr);
}
*/

static void deleteDiskRequest(DiskRequest *dr){
	releaseReservedPage(dr->physicalBufferManager, dr->physicalBufferPage);
	if(hasSeparateSectorBuffer(dr)){
		if(checkAndReleaseKernelPages(dr->sectorBufferPage) == 0){
			panic("");
		}
	}
	DELETE(dr);
}

static DiskRequest *createRWDiskRequest(
	enum ATACommand cmd, RWFileRequest *rwfr,
	void *buffer, uintptr_t bufferSize, uint64_t position,
	AHCIInterruptArgument *a, int portIndex, char isWrite
){
	const uintptr_t sectorSize = a->desc.sectorSize;
	const uintptr_t sectorBufferSize = CEIL(position + bufferSize, sectorSize) - FLOOR(position, sectorSize);
	// assert(PAGE_SIZE % a->desc.sectorSize == 0);
	// TODO: allocate continuous physical pages or send multiple commands
	// read sector size <= PAGE_SIZE and buffer resides in one page
	if(sectorBufferSize > PAGE_SIZE ||
		CEIL(((uintptr_t)buffer) + bufferSize, PAGE_SIZE) - FLOOR((uintptr_t)buffer, PAGE_SIZE) > PAGE_SIZE
	){
		return NULL;
	}
	DiskRequest *NEW(dr);
	EXPECT(dr != NULL);
	dr->rwfr = rwfr;
	dr->ior = NULL;
	dr->lock = &a->lock;
	dr->command = cmd;
	dr->inputBuffer = buffer;
	dr->inputSize = bufferSize;

	// buffer aligned to sector && size aligned to sector && resides in one page && position aligned to sector
	if(
		((uintptr_t)buffer) % a->desc.sectorSize == 0 &&
		bufferSize == sectorBufferSize &&
		position % a->desc.sectorSize == 0
	){ // aligned
		dr->sectorBufferOffset =
		dr->bufferOffset = ((uintptr_t)buffer) % PAGE_SIZE;
		dr->sectorBufferPage = (void*)(((uintptr_t)buffer) - dr->bufferOffset);
		dr->physicalBufferManager = getTaskLinearMemory(processorLocalTask());
	}
	else{ // not aligned
		dr->sectorBufferOffset = 0;
		dr->bufferOffset = position % sectorSize;
		dr->sectorBufferPage = allocateKernelPages(CEIL(sectorBufferSize, PAGE_SIZE), KERNEL_NON_CACHED_PAGE);
		dr->physicalBufferManager  = kernelLinear;
	}
	EXPECT(dr->sectorBufferPage != NULL);
	dr->physicalBufferPage = checkAndReservePage(dr->physicalBufferManager, dr->sectorBufferPage, KERNEL_PAGE);
	EXPECT(dr->physicalBufferPage.value != INVALID_PAGE_ADDRESS);
	dr->lba = position / sectorSize;
	dr->sectorCount = sectorBufferSize / sectorSize;
	dr->ahci = a;
	dr->portIndex = portIndex;
	dr->isWrite = isWrite;
	dr->prev = NULL;
	dr->next = NULL;
	return dr;
	//releaseReservedPage(dr->bufferMemory, dr->buffer);
	ON_ERROR;
	if(hasSeparateSectorBuffer(dr)){
		checkAndReleaseKernelPages(dr->sectorBufferPage);
	}
	ON_ERROR;
	DELETE(dr);
	ON_ERROR;
	return NULL;
}

static int acceptIdentifyDiskRequest(void *instance, uintptr_t *returnValues){
	DiskRequest *dr = instance;
	returnValues[0] = DEFAULT_SECTOR_SIZE;//(dr->sectorCount * dr->ahci->desc.sectorSize);
	deleteDiskRequest(dr);
	return 1;
}

static DiskRequest *createIdentifyDiskRequest(
	enum ATACommand cmd, IORequest *ior,
	void *buffer, uintptr_t bufferSize,
	AHCIInterruptArgument *a, int portIndex
){
	DiskRequest *NEW(dr);
	EXPECT(dr != NULL);
	initIORequest(ior, dr, notSupportCancelIO, acceptIdentifyDiskRequest);
	dr->rwfr = NULL;
	dr->ior = ior;
	dr->lock = &a->lock;
	dr->command = cmd;
	assert(((uintptr_t)buffer) % PAGE_SIZE == 0 && bufferSize == DEFAULT_SECTOR_SIZE);
	dr->inputBuffer = buffer;
	dr->inputSize = bufferSize;
	dr->sectorBufferOffset = 0;
	dr->bufferOffset = 0;
	dr->physicalBufferManager = getTaskLinearMemory(processorLocalTask());
	dr->sectorBufferPage = buffer;
	dr->physicalBufferPage = checkAndReservePage(dr->physicalBufferManager, buffer, KERNEL_PAGE); // at least 512 bytes
	EXPECT(dr->physicalBufferPage.value != INVALID_PAGE_ADDRESS);
	dr->lba = 0; // ignored
	dr->sectorCount = 0; // ignored
	dr->ahci = a;
	dr->portIndex = portIndex;
	dr->isWrite = 0;
	dr->prev = NULL;
	dr->next = NULL;
	return dr;
	//releaseReservedPage(kernelLinear, buffer);
	ON_ERROR;
	DELETE(dr);
	ON_ERROR;
	return NULL;
}

// disk request queue
typedef struct AHCIManager{
	Spinlock lock;
	AHCIInterruptArgument *ahciList;
	int ahciCount;
}AHCIManager;
// must be in kernel space
static AHCIManager ahciManager = {INITIAL_SPINLOCK, NULL, 0};

// return 0 if failed to issue command
// return 1 if command was issued or pended
static int servePortQueue(AHCIInterruptArgument *a, int portIndex){
	assert(isAcquirable(&a->lock) == 0);
	if(a->port[portIndex].servingRequest != NULL){
		return 1;
	}
	DiskRequest *dr = a->port[portIndex].pendingRequest;
	if(dr == NULL){
		return 1;
	}
	REMOVE_FROM_DQUEUE(dr);
	ADD_TO_DQUEUE(dr, &a->port[portIndex].servingRequest);
	return sendDiskRequest(dr);
}

static void addToPortQueue(DiskRequest *dr, /*hba, */int portIndex){
	assert(isAcquirable(dr->lock) == 0);
	struct AHCIPortQueue *p = dr->ahci->port + portIndex;
	ADD_TO_DQUEUE(dr, &p->pendingRequest);
}

static DiskRequest *removeFromPortQueue(AHCIInterruptArgument *a, int portIndex){
	assert(isAcquirable(&a->lock) == 0);
	AHCIPortQueue *p = &a->port[portIndex];
	DiskRequest *dr = p->servingRequest;
	if(dr != NULL){
		REMOVE_FROM_DQUEUE(dr);
	}
	return dr;
}

// interrupt & system call

static void AHCIHandler(InterruptParam *param){
	AHCIInterruptArgument *arg = (AHCIInterruptArgument*)param->argument;
	DiskRequest *completedRW = NULL;
	uint32_t hostStatus = arg->hbaRegisters->interruptStatus;
	// software writes 1 to indicate the interrupt has been handled
	arg->hbaRegisters->interruptStatus = hostStatus;
	int p;
	for(p = 0; p < HBA_MAX_PORT_COUNT; p++){
		if((hostStatus & (1 << p)) == 0)
			continue;
		uint32_t portStatus = arg->hbaRegisters->port[p].interruptStatus;
		if(portStatus == 0)
			continue;
		arg->hbaRegisters->port[p].interruptStatus = portStatus;

		acquireLock(&arg->lock);
		DiskRequest *dr = removeFromPortQueue(arg, p);
		if(servePortQueue(arg, p) == 0){
			panic("servePortQueue == 0"); // TODO: how to handle?
		}
		releaseLock(&arg->lock);
		if(dr == NULL)
			printk("warning: AHCI driver received unexpected interrupt\n");
		else{
			// cannot completeRWFileIO() before sti(), so put requests into the queue
			ADD_TO_DQUEUE(dr, &completedRW);
		}
	}
	processorLocalPIC()->endOfInterrupt(param);
	sti();
	while(completedRW != NULL){
		DiskRequest *dr = completedRW;
		REMOVE_FROM_DQUEUE(dr);
		if(hasSeparateSectorBuffer(dr)){
			memcpy(dr->inputBuffer, diskLinearBuffer(dr), dr->inputSize);
		}
		if(dr->command == IDENTIFY_DEVICE){
			assert(dr->rwfr == NULL && dr->ior != NULL);
			completeIO(dr->ior);
		}
		else{
			assert(dr->rwfr != NULL && dr->ior == NULL);
			completeRWFileIO(dr->rwfr, dr->inputSize);
			deleteDiskRequest(dr);
		}
	}
	// printk("end of AHCI interrupt\n");
}

// buffer address
static_assert(sizeof(uint32_t) == sizeof(uintptr_t));

#pragma pack(1)

union HBAPortIndex{
	struct{
		uint16_t hbaIndex;
		uint8_t portIndex;
		uint8_t unused;
	};
	uintptr_t value;
}__attribute__((__packed__));

typedef union HBAPortIndex HBAPortIndex;

#pragma pack()

static_assert(sizeof(HBAPortIndex) == sizeof(uint32_t));

static_assert(HBA_MAX_PORT_COUNT < 0x1000);

static AHCIInterruptArgument *searchHBAByPortIndex(AHCIManager *am, HBAPortIndex index){
	AHCIInterruptArgument *a = NULL;
	if(/*index.portIndex < 0 || */index.portIndex >= HBA_MAX_PORT_COUNT)
		return NULL;
	acquireLock(&am->lock);
	if(/*index.hbaIndex >= 0 && */index.hbaIndex < am->ahciCount){
		for(a = am->ahciList; a != NULL; a = a->next){
			if(a->hbaIndex == index.hbaIndex){
				break;
			}
		}
	}
	releaseLock(&am->lock);
	if(a == NULL){
		return NULL;
	}
	return hasPort(a, index.portIndex)? a: NULL;
}

static HBAPortIndex toDiskCode(int a, int p){
	HBAPortIndex i = {value: 0};
	i.hbaIndex = a;
	i.portIndex = p;
	i.unused = 0;
	return i;
}

static int initDiskDescription(struct DiskDescription *d, AHCIInterruptArgument *arg){
	uint16_t *buffer = systemCall_allocateHeap(DEFAULT_SECTOR_SIZE, KERNEL_NON_CACHED_PAGE);
	EXPECT(buffer != NULL);
	memset(buffer, 0, DEFAULT_SECTOR_SIZE);
	IORequest ior;

	DiskRequest *dr = createIdentifyDiskRequest(
		IDENTIFY_DEVICE, &ior,
		buffer, DEFAULT_SECTOR_SIZE, arg, arg->hbaIndex
	);
	EXPECT(dr != NULL);
	pendIO(dr->ior);
	acquireLock(dr->lock);
	addToPortQueue(dr, /*hba, */dr->portIndex);
	if(servePortQueue(dr->ahci, dr->portIndex) == 0){
		assert(0);
		// TODO: how to handle?
	}
	releaseLock(dr->lock);

	uintptr_t waitIOR = systemCall_waitIO((uintptr_t)dr->ior);
	assert(waitIOR == (uintptr_t)dr->ior);

	// the driver requires 48-bit address
	const uint16_t buffer83 = buffer[83];
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
		d->sectorSize = buffer[117] + (((uintptr_t)buffer[118]) << 16);
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

static AHCIInterruptArgument *initAHCI(AHCIManager *am, const PCIConfigRegisters0 *regs){
	PIC *pic = processorLocalPIC();
	AHCIInterruptArgument *arg = initAHCIRegisters(regs->bar5);
	if(arg == NULL){
		return NULL;
	}
	// add to manager
	acquireLock(&am->lock);
	arg->hbaIndex = (uint16_t)am->ahciCount;
	arg->next = am->ahciList;
	am->ahciList = arg;
	am->ahciCount++;
	releaseLock(&am->lock);

	InterruptVector *v = pic->irqToVector(pic, regs->interruptLine);
	setHandler(v, AHCIHandler, (uintptr_t)arg);
	pic->setPICMask(pic, regs->interruptLine, 0);
	return arg;
}

// file interface

static int seekReadAHCI(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uint64_t position, uintptr_t bufferSize){
	// TODO: allow multiple pages/physical regions
	HBAPortIndex index;
	index.value = (uintptr_t)getFileInstance(of);
	AHCIInterruptArgument *hba = searchHBAByPortIndex(&ahciManager, index);
	EXPECT(hba != NULL);

	EXPECT(position < hba->desc.sectorCount * hba->desc.sectorSize &&
		position + bufferSize <= hba->desc.sectorCount * hba->desc.sectorSize);
	DiskRequest *dr = createRWDiskRequest(
		(0? DMA_WRITE_EXT: DMA_READ_EXT), rwfr,
		buffer, bufferSize, position,
		hba, index.portIndex, 0
	);
	EXPECT(dr != NULL);
	// send DiskRequest
	pendRWFileIO(rwfr);
	//setRWFileIOFunctions(rwfr, dr, cancelRWAHCI);
	acquireLock(dr->lock);
	addToPortQueue(dr, /*hba, */dr->portIndex);
	if(servePortQueue(dr->ahci, dr->portIndex) == 0){
		assert(0);
		// TODO: how to handle?
	}
	releaseLock(dr->lock);
	return 1;
	//deleteDiskRequest(dr);
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	return 0;
}

static void closeAHCI(CloseFileRequest *cfr, __attribute__((__unused__)) OpenedFile *of){
	pendCloseFileIO(cfr);
	completeCloseFile(cfr);
	// do not delete of->instance
}

static int openAHCI(
	OpenFileRequest *ofr,
	const char *fileName, uintptr_t length, __attribute__((__unused__)) OpenFileMode mode
){
	uintptr_t index;
	if(snscanf(fileName, length, "%x", &index) != 1){
		return 0;
	}
	AHCIInterruptArgument *arg = searchHBAByPortIndex(&ahciManager, (HBAPortIndex)index);
	if(arg == NULL){
		return 0;
	}
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.seekRead = seekReadAHCI;
	//if(mode.writable){
	//	ff.write = seekWriteAHCI;
	//}
	ff.close = closeAHCI;
	pendOpenFileIO(ofr);
	completeOpenFile(ofr, (void*)index, &ff);
	return 1;
}

/*
static IORequest *seekWriteAHCI(OpenFileRequest *ofr, const uint8_t *buffer, uint64_t position, uintptr_t bufferSize){

}
*/

void ahciDriver(void){
	const char *driverName = "ahci";
	// 0x01: mass storage; 0x06: SATA; 01: AHCI >= 1.0
	uintptr_t enumPCI = enumeratePCI(0x01060100, 0xffffff00);
	assert(enumPCI != IO_REQUEST_FAILURE);
	FileNameFunctions ff = INITIAL_FILE_NAME_FUNCTIONS;
	ff.open = openAHCI;
	addFileSystem(&ff, driverName, strlen(driverName));
	while(1){
		PCIConfigRegisters pciConfig;
		PCIConfigRegisters0 *regs0 = &pciConfig.regs0;
		if(nextPCIConfigRegisters(enumPCI, &pciConfig, sizeof(*regs0)) != sizeof(*regs0))
			break;
		AHCIInterruptArgument *arg = initAHCI(&ahciManager, regs0);

		int p;
		for(p = 0; p < HBA_MAX_PORT_COUNT; p++){
			if(hasPort(arg, p) == 0){
				continue;
			}
			const HBAPortIndex diskCode = toDiskCode(arg->hbaIndex, p);
			char fileName[20];
			snprintf(fileName, 20, "%s:%x", driverName, diskCode.value);
			if(initDiskDescription(&arg->desc, arg) == 0){
				printk("identify hba %d port %d failed\n", arg->hbaIndex, p);
				continue;
			}
			if(PAGE_SIZE % arg->desc.sectorSize != 0){
				printk("warning: unsupported ahci disk sector size: %u\n", arg->desc.sectorSize);
			}
			uintptr_t fileHandle = syncOpenFile(fileName);
			assert(fileHandle != IO_REQUEST_FAILURE);
			readPartitions(driverName, fileHandle, 0,
			arg->desc.sectorCount, arg->desc.sectorSize, diskCode.value);
			uintptr_t r = syncCloseFile(fileHandle);
			assert(r != IO_REQUEST_FAILURE);
		}
	}
	syncCloseFile(enumPCI);
	while(1){
		sleep(1000);
	}
}

#ifndef NDEBUG
void testAHCI(void);
void testAHCI(void){
	printk("test ahci driver...\n");
	uintptr_t d = systemCall_discoverDisk(MBR_FAT32);
	assert(d != IO_REQUEST_FAILURE);
	uintptr_t lbaLow = 0, lbaHigh = 0;
	uint64_t lba;
	HBAPortIndex pi;
	uintptr_t sectorSize;
	uintptr_t r = systemCall_waitIOReturn(d, 4, &lbaLow, &lbaHigh, &pi.value, &sectorSize);
	lba = COMBINE64(lbaLow, lbaHigh);
	assert(r == d);
	char s[20];
	snprintf(s, 20, "ahci:%x", pi.value);
	//printk("open %s\n",s);
	uintptr_t h = syncOpenFile(s);
	assert(h != IO_REQUEST_FAILURE);
	int i;
	uint8_t *buffer = systemCall_allocateHeap(PAGE_SIZE, USER_WRITABLE_PAGE);
	assert(buffer != NULL);
	for(i = 0; i < 30; i++){
		uintptr_t bs = PAGE_SIZE;
		memset(buffer, 9, PAGE_SIZE);
		r = syncSeekReadFile(h, buffer, lba * sectorSize + i * PAGE_SIZE, &bs);
		assert(r != IO_REQUEST_FAILURE && bs == PAGE_SIZE);
		uintptr_t j;
		for(j = 0; j < bs; j++){
			if(buffer[j] != 9){
				break;
			}
		}
		assert(j != bs);
		;
		// test non aligned buffer
		for(j = 0; j < 20; j++){
			uintptr_t j2 = ((j + 19) * (i + 11) % PAGE_SIZE);
			uint8_t buffer1[3] = {9, 9, 9};
			bs = 1;
			r = syncSeekReadFile(h, &buffer1[1], lba * sectorSize + i * PAGE_SIZE + j2, &bs);
			assert(r != IO_REQUEST_FAILURE && bs == 1);
			assert(buffer1[0] == 9 && buffer1[1] == buffer[j2] && buffer1[2] == 9);
		}
	}
	r = systemCall_releaseHeap(buffer);
	assert(r);
	r = syncCloseFile(h);
	assert(r != IO_REQUEST_FAILURE);
	printk("test ahci ok\n");
	systemCall_terminate();
}
#endif
