#include"io.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/controller/pic.h"
#include"task/exclusivelock.h"
#include"file/file.h"

typedef struct{
	uint64_t address;
	uint16_t length;
	uint16_t checksum; // not supported in some chips
	uint8_t status;
	uint8_t errors;
	uint16_t special; // not support in some chips
}ReceiveDescriptor;

static_assert(sizeof(ReceiveDescriptor) == 16);

typedef struct{
	uint8_t ipChecksumStart;
	uint8_t ipChecksumOffset;
	uint16_t ipChecksumEnd;
	uint8_t tuChecksumStart;
	uint8_t tuChecksumOffset;
	uint16_t tuChecksumEnd;
	uint32_t payloadLength: 20;
	uint32_t type: 4;
	uint32_t tuCommand: 8;
	uint8_t status: 4;
	uint8_t reserved: 4;
	uint8_t headerLength;
	uint16_t maxSegmentSize;
}ContextTransmitDescriptor;

static_assert(sizeof(ContextTransmitDescriptor) == 16);

typedef struct{
	uint64_t address;
	uint32_t dataLength: 20;
	uint32_t type: 4;
	uint32_t descriptorCommand: 8;
	uint8_t status: 4;
	uint8_t reserved: 4;
	uint8_t packetOption;
	uint16_t special;
}DataTransmitDescriptor;

static_assert(sizeof(DataTransmitDescriptor) == 16);

typedef union{
	ContextTransmitDescriptor context;
	DataTransmitDescriptor data;
}TransmitDescriptor;

static_assert(sizeof(TransmitDescriptor) == 16);

#define S (sizeof(uint32_t))
enum I8254xRegisterIndex{
	DEVICE_CONTROL = 0x00000 / S,
	DEVICE_STATUS = 0x0008 / S,

	INTERRUPT_CAUSE_READ = 0x00c0 / S,
	// INTERRUPT_THROTTLING = 0x00c4 / S,
	INTERRUPT_CAUSE_SET = 0x00c8 / S,
	INTERRUPT_MASK_SET_READ = 0x00d0 / S,
	INTERRUPT_MASK_CLEAR = 0x00d8 / S,

	RECEIVE_CONTROL = 0x0100 / S,
	RECEIVE_DESCRIPTORS_BASE_LOW = 0x2800 / S,
	RECEIVE_DESCRIPTORS_BASE_HIGH = 0x2804 / S,
	RECEIVE_DESCRIPTORS_LENGTH = 0x2808 / S,
	RECEIVE_DESCRIPTORS_HEAD = 0x2810 / S,
	RECEIVE_DESCRIPTORS_TAIL = 0x2818 / S,
	RECEIVE_DELAY_TIMER = 0x2820 / S,

	TRANSMIT_CONTROL = 0x400 / S,
	TRANSMIT_DESCRIPTORS_BASE_LOW = 0x3800 / S,
	TRANSMIT_DESCRIPTORS_BASE_HIGH = 0x3804 / S,
	TRANSMIT_DESCRIPTORS_LENGTH = 0x3808 / S,
	TRANSMIT_DESCRIPTORS_HEAD = 0x3810 / S,
	TRANSMIT_DESCRIPTORS_TAIL = 0x3818 / S,
	TRANSMIT_DELAY_TIMER = 0x03820 / S,

	MULTICAST_TABLE_ARRAY = 0x5200 / S,
	RECEIVE_ADDRESS_0_LOW = 0x5400 / S,
	RECEIVE_ADDRESS_0_HIGH = 0x5404 / S
};
#undef S
#define MULTICAST_TABLE_ARRAY_LENGTH (128)
#define I8254X_REGISTERS_SIZE (0x20000)

// for INTERRUPT_XX registers
enum InterruptBit{
	TRANSMIT_DESC_WRITTEN_BACK_BIT = (1 << 0),
	TRANSMIT_QUEUE_EMPTY_BIT = (1 << 1),
	LINK_STATUS_CHANGE_BIT = (1 << 2),
	//RECEIVE_SEQUENCE_ERROR_BIT = (1 << 3),
	RECEIVE_THRESHOLD_REACHED_BIT = (1 << 4),
	//reserve (1 << 5)
	RECEIVER_OVERRUN_BIT = (1 << 6),
	RECEIVER_TIMER_EXPIRE_BIT = (1 << 7)
};

typedef union{
	uint32_t value;
	struct{
		uint32_t reserved0: 1;
		uint32_t enabled: 1;
		uint32_t storeBadPackets: 1;
		uint32_t unicastPromiscuous: 1;
		uint32_t multicastPromiscuous: 1;
		uint32_t longPacketReception: 1;
		uint32_t loopbackMode: 2;
		// 8
		uint32_t receiveDescThreshold: 2;
		uint32_t reserved1: 2;
		uint32_t multicastOffset: 2;
		uint32_t reserved2: 1;
		uint32_t broadacastAcceptMode: 1;
		// 16
		uint32_t receiveBufferSize: 2;
		uint32_t vlanFilter: 1;
		uint32_t canonicalFormIndicator: 1;
		uint32_t canonicalFormIndicatorValue: 1;
		uint32_t reserved3: 1;
		uint32_t discardPauseFrames: 1;
		uint32_t passMACControlFrames: 1;
		// 24
		uint32_t reserved4: 1;
		uint32_t bufferSizeExtension: 1;
		uint32_t stripEthernetCRC: 1;
		uint32_t reserved5: 5;
	};
}ReceiveControlRegister;

static_assert(sizeof(ReceiveControlRegister) == sizeof(uint32_t));

#define RECEIVE_DESCRIPTOR_BUFFER_SIZE (256)
#define TRANSMIT_DESCRIPTOR_BUFFER_SIZE (256)

typedef struct{
	volatile uint32_t *regs;
	uintptr_t receiveDescCnt; // size is multiple of 128 bytes
	volatile ReceiveDescriptor *receiveDesc; // aligned to 16-byte
	volatile uint8_t (*receiveBuffer)[RECEIVE_DESCRIPTOR_BUFFER_SIZE];
	// handlerBegin > receiverBegin
	// receiveDesc[driverBegin - 1] is not used to prevent overlapping
	uintptr_t intBegin, taskBegin;
	Semaphore *semaphore;
}I8254xReceive;

typedef struct{
	uintptr_t transmitDescCnt; // size is multiple of 128 bytes
	volatile union{
		ContextTransmitDescriptor context;
		DataTransmitDescriptor data;  // aligned to 16-bit
	}*transmitDesc; //PAGE_SIZE / sizeof(DataTransmitDescriptor);
	volatile uint8_t (*transmitBuffer)[TRANSMIT_DESCRIPTOR_BUFFER_SIZE];
	uintptr_t intBegin, taskBegin;
	Semaphore *semaphore;
}I8254xTransmit;


typedef struct I8254xReadRequest{
	RWFileRequest *rwfr;

	struct I8254xReadRequest **prev, *next;
}I8254xReadRequest;
/*
static I8254xReadRequest *createReadRequest(RWFileRequest *rwfr, uint8_t *buffer, uintptr_t readSize){
	I8254xReadRequest *NEW(r);
	if(r == NULL){
		return NULL;
	}
	r->rwfr = rwfr;
	r->prev = NULL;
	r->next = NULL;
	return r;
}
*/
typedef struct I8254xDevice{
	volatile uint32_t *regs;

	int terminateFlag;
	I8254xTransmit transmit;
	Task *transmitTask;
	I8254xReceive receive;
	Task *receiveTask;

	int serialNumber;
	Spinlock lock;
	I8254xReadRequest *pending, *receiving;

	struct I8254xDevice *next, **prev;
}I8254xDevice;

static_assert(sizeof(((I8254xReceive*)0)->receiveBuffer[0]) == RECEIVE_DESCRIPTOR_BUFFER_SIZE);
static_assert(sizeof(((I8254xTransmit*)0)->transmitBuffer[0]) == RECEIVE_DESCRIPTOR_BUFFER_SIZE);

static int initI8254Receive(I8254xReceive *r, volatile uint32_t *regs){
	r->regs = regs;
	const uintptr_t descArraySize = PAGE_SIZE;
	// 4096 / 16 = 256
	r->receiveDescCnt = descArraySize / sizeof(ReceiveDescriptor);
	r->receiveDesc = allocatePages(kernelLinear, descArraySize, KERNEL_NON_CACHED_PAGE);
	EXPECT(r->receiveDesc != NULL);
	// 256 * 256 = 65536
	r->receiveBuffer = allocatePages(kernelLinear,
		sizeof(r->receiveBuffer[0]) * r->receiveDescCnt, KERNEL_NON_CACHED_PAGE);
	EXPECT(r->receiveBuffer != NULL);
	// see regs[RECEIVE_DESCRIPTORS_HEAD]
	r->intBegin = 0;
	r->taskBegin = 0;
	r->semaphore = createSemaphore();
	EXPECT(r->semaphore != NULL);
	// set mac address
	// device->regs[RECEIVE_ADDRESS_0_HIGH] =
	// device->regs[RECEIVE_ADDRESS_0_LOW] =
	// clear multicast table array
	uintptr_t i;
	for(i = 0; i < MULTICAST_TABLE_ARRAY_LENGTH; i++){
		regs[MULTICAST_TABLE_ARRAY + i] = 0;
	}
	// disable receive delay timer
	regs[RECEIVE_DELAY_TIMER] = 0;
	// write 1 to mask to disable interrupt
	regs[INTERRUPT_MASK_CLEAR] = (LINK_STATUS_CHANGE_BIT | RECEIVE_THRESHOLD_REACHED_BIT);
	// write 1 to set to enable interrupt
	regs[INTERRUPT_MASK_SET_READ] = (LINK_STATUS_CHANGE_BIT | RECEIVE_THRESHOLD_REACHED_BIT);
	// descriptor array
	const uintptr_t rdArraySize = r->receiveDescCnt * sizeof(ReceiveDescriptor);
	assert(rdArraySize > 0 && rdArraySize <= PAGE_SIZE && rdArraySize % 128 == 0);
	uint64_t rdPhysical = checkAndTranslatePage(kernelLinear, (void*)r->receiveDesc).value;
	assert((uintptr_t)rdPhysical != INVALID_PAGE_ADDRESS);
	regs[RECEIVE_DESCRIPTORS_BASE_LOW] = LOW64(rdPhysical);
	regs[RECEIVE_DESCRIPTORS_BASE_HIGH] = HIGH64(rdPhysical);
	regs[RECEIVE_DESCRIPTORS_LENGTH] = rdArraySize;
	// descriptor buffer address
	for(i = 0; i < r->receiveDescCnt; i++){
		const uintptr_t offset = ((uintptr_t)r->receiveBuffer[i]) % PAGE_SIZE;
		const uintptr_t pageAddress = ((uintptr_t)r->receiveBuffer[i]) - offset;
		PhysicalAddress physicalAddress = checkAndTranslatePage(kernelLinear, (void*)pageAddress);
		assert(physicalAddress.value != INVALID_PAGE_ADDRESS);
		MEMSET0((void*)&r->receiveDesc[i]);
		r->receiveDesc[i].address = physicalAddress.value + offset;
		//MEMSET0(device->receiveBuffer[i]);
	}
	regs[RECEIVE_DESCRIPTORS_HEAD] = 0;
	regs[RECEIVE_DESCRIPTORS_TAIL] = r->receiveDescCnt - 1;
	ReceiveControlRegister rc;
	rc.value = 0;
	rc.enabled = 1;
	// do not filter destination address
	rc.unicastPromiscuous = 1;
	rc.multicastPromiscuous = 1;
	// 0 = 1/2 of descriptor array; 1 = 1/4; 2 = 1/8
	// 4096/16/8=32
	rc.receiveDescThreshold = 2;
	// bit 36~48 as lookup address
	// IMPROVE: rc.multicastOffset = 0
	// accept broadcast
	rc.broadacastAcceptMode = 1;
	// 0 = 2048; 1 = 1024; 2 = 512; 3 = 256
	switch(TRANSMIT_DESCRIPTOR_BUFFER_SIZE){
	case 256:
	case 256*16:
		rc.receiveBufferSize = 3;
		break;
	case 512:
	case 512 * 16:
		rc.receiveBufferSize = 2;
		break;
	case 1024:
	case 1024 * 16:
		rc.receiveBufferSize = 1;
		break;
	case 2048:
	case 2048 * 16:
		rc.receiveBufferSize = 0;
		break;
	}
	//0 = 1 byte; 1 = 16 bytes
	rc.bufferSizeExtension = (TRANSMIT_DESCRIPTOR_BUFFER_SIZE >= 4096? 1: 0);
	regs[RECEIVE_CONTROL] = rc.value;

	return 1;
	deleteSemaphore(r->semaphore);
	ON_ERROR;
	checkAndReleasePages(kernelLinear, r->receiveBuffer);
	ON_ERROR;
	checkAndReleasePages(kernelLinear, (void*)r->receiveDesc);
	ON_ERROR;
	return 0;
}

static void deleteI8254xReceive(I8254xReceive* rece){
	// reset register
	assert(getSemaphoreValue(rece->semaphore) == 0 &&
		rece->receiveBuffer != NULL && rece->receiveDesc != NULL);
	deleteSemaphore(rece->semaphore);
	checkAndReleasePages(kernelLinear, rece->receiveBuffer);
	checkAndReleasePages(kernelLinear, (void*)rece->receiveDesc);
}

static int initI8254xTransmit(I8254xTransmit *t, volatile uint32_t *regs){
	const uintptr_t descArraySize = PAGE_SIZE;
	// 4096 / 16 = 256
	t->transmitDescCnt = descArraySize / sizeof(ContextTransmitDescriptor);
	t->transmitDesc = allocatePages(kernelLinear, descArraySize, KERNEL_NON_CACHED_PAGE);
	EXPECT(t->transmitDesc != NULL);

	// 256 * 256 = 65536
	t->transmitBuffer = allocatePages(kernelLinear,
		sizeof(t->transmitBuffer[0]) * t->transmitDescCnt, KERNEL_NON_CACHED_PAGE);
	EXPECT(t->transmitBuffer != NULL);

	t->intBegin = 0;
	t->taskBegin = 0;
	t->semaphore = createSemaphore();
	EXPECT(t->semaphore != NULL);
	// TODO: iniI8254xReceive also sets LINK_STATUS
	regs[INTERRUPT_MASK_CLEAR] |=
		(TRANSMIT_DESC_WRITTEN_BACK_BIT | TRANSMIT_QUEUE_EMPTY_BIT | LINK_STATUS_CHANGE_BIT);
	regs[INTERRUPT_MASK_SET_READ] |=
		(TRANSMIT_DESC_WRITTEN_BACK_BIT | TRANSMIT_QUEUE_EMPTY_BIT | LINK_STATUS_CHANGE_BIT);
	// descriptor array
	const uintptr_t tdArraySize = t->transmitDescCnt * sizeof(TransmitDescriptor);
	uint64_t tdAddress = checkAndTranslatePage(kernelLinear, (void*)t->transmitDesc).value;
	regs[TRANSMIT_DESCRIPTORS_BASE_LOW] = LOW64(tdAddress);
	regs[TRANSMIT_DESCRIPTORS_BASE_HIGH] = HIGH64(tdAddress);
	regs[TRANSMIT_DESCRIPTORS_LENGTH] = tdArraySize;
	regs[TRANSMIT_DESCRIPTORS_HEAD] = 0;
	regs[TRANSMIT_DESCRIPTORS_TAIL] = 0;
	// descriptor buffer address

	//TODO:
	// 100Mbps = 12.5 byte/usec
	// 1000 * 12.5 = 12500
	regs[TRANSMIT_DELAY_TIMER] = 1000; // in 1.024 usec
	return 1;
	//deleteSemaphore(t->semaphore)
	ON_ERROR;
	checkAndReleasePages(kernelLinear, (void*)t->transmitBuffer);
	ON_ERROR;
	checkAndReleasePages(kernelLinear, (void*)t->transmitDesc);
	ON_ERROR;
	return 0;
}

static void deleteI8254xTransmit(I8254xTransmit *tra){
	assert(tra->transmitBuffer != NULL && tra->transmitDesc != NULL);
	checkAndReleasePages(kernelLinear, (void*)tra->transmitBuffer);
	checkAndReleasePages(kernelLinear, (void*)tra->transmitDesc);
}

static void i8254xReceiveTask(void *arg){
	I8254xDevice *d = *(I8254xDevice**)arg;
	I8254xReceive *r = &d->receive;

	printk("receiver started %d\n", r->receiveDescCnt);
	while(1){
		acquireSemaphore(r->semaphore);
		printk("received\n");
		const uintptr_t i = r->taskBegin;
		printk("%d\n", r->receiveDesc[i].length);
		unsigned a;
		for(a = 0; a < r->receiveDesc[i].length; a++){
			printk("%x \n", r->receiveBuffer[i][a]);
		}
		assert((r->regs[RECEIVE_DESCRIPTORS_TAIL] + 1) % r->receiveDescCnt == i);
		MEMSET0((void*)&r->receiveDesc[i]);
		r->regs[RECEIVE_DESCRIPTORS_TAIL] = i;
		r->taskBegin = i + 1;
	}

	systemCall_terminate();
}

static void i8254xTransmitTask(void *arg){
	I8254xDevice *d = *(I8254xDevice**)arg;
	I8254xTransmit *t = &d->transmit;

	printk("transmitter started %d\n", t->transmitDescCnt);
	while(1){
		acquireSemaphore(t->semaphore);
	}

	systemCall_terminate();
}

static I8254xDevice *createI8254xDevice(PCIConfigRegisters0 *pciRegs, int number){
	I8254xDevice *NEW(device);
	EXPECT(device != NULL);
	device->serialNumber = number;
	device->lock = initialSpinlock;
	PhysicalAddress pa = {pciRegs->bar0 & 0xfffffff0};
	device->regs = mapKernelPages(pa, I8254X_REGISTERS_SIZE, KERNEL_NON_CACHED_PAGE);
	EXPECT(device->regs != NULL);

	device->terminateFlag = 0;
	int ok = initI8254Receive(&device->receive, device->regs);
	EXPECT(ok);
	ok = initI8254xTransmit(&device->transmit, device->regs);
	EXPECT(ok);
	device->receiveTask = createSharedMemoryTask(i8254xReceiveTask, &device, sizeof(device), processorLocalTask());
	EXPECT(device->receiveTask != NULL);
	device->transmitTask = createSharedMemoryTask(i8254xTransmitTask, &device, sizeof(device), processorLocalTask());
	EXPECT(device->transmitTask != NULL);

	resume(device->receiveTask);
	resume(device->transmitTask);

	//printk("%x %x %x\n", device->regs[DEVICE_CONTROL], device->regs[DEVICE_STATUS], device->regs[RECEIVE_CONTROL]);

	return device;
	ON_ERROR;
	device->terminateFlag = 1;
	// TODO: kill and wait tasks
	ON_ERROR;
	device->regs[DEVICE_CONTROL] = (1 << 26); // trigger reset
	//while(device->regs[DEVICE_CONTROL] & (1 << 26)); poll until the bit is cleared
	deleteI8254xTransmit(&device->transmit);
	ON_ERROR;
	deleteI8254xReceive(&device->receive);
	ON_ERROR;
	unmapKernelPages((void*)device->regs);
	ON_ERROR;
	DELETE(device);
	ON_ERROR;
	return NULL;
}

static int i8254xReceiveHandler(I8254xReceive *rece){
	uintptr_t i;
	for(i = 0; i < rece->receiveDescCnt - 1; i++){
		uintptr_t d = (rece->intBegin + i) % rece->receiveDescCnt;
		// send to receiver service
		releaseSemaphore(rece->semaphore);
		// until descriptor is not done
		if((rece->receiveDesc[d].status & 1) == 0)
			break;
	}
	rece->intBegin = (rece->intBegin + i) % rece->receiveDescCnt;
	//regs[RECEIVE_DESCRIPTORS_TAIL] =
	//	(rece->receiveHead + rece->receiveDescCnt - 1) % rece->receiveDescCnt;
	return i > 0;
}

static int i8254xTransmitHandler(__attribute__((__unused__)) I8254xTransmit *t){
	//TODO:
	return 0;
}

static int i8254xHandler(const InterruptParam *p){
	I8254xDevice *i8254x = (I8254xDevice*)p->argument;
	// reading the register implicitly clears interrupt status
	uint32_t cause = i8254x->regs[INTERRUPT_CAUSE_READ];
	int handled = (cause != 0);
	if(cause & LINK_STATUS_CHANGE_BIT){
		printk("link status change: %x\n", ((i8254x->regs[DEVICE_STATUS] >> 1) & 1));
	}
	if(cause & (RECEIVE_THRESHOLD_REACHED_BIT | RECEIVER_OVERRUN_BIT | RECEIVER_TIMER_EXPIRE_BIT)){
		i8254xReceiveHandler(&i8254x->receive);
	}
	if(cause & (TRANSMIT_DESC_WRITTEN_BACK_BIT | TRANSMIT_QUEUE_EMPTY_BIT)){
		i8254xTransmitHandler(&i8254x->transmit);
	}
	return handled;
}
/*
int readI8254x(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t readSize){
	I8254xDevice *device = getFileInstance(of);

	I8254xReadRequest *r = createReadRequest(rwfr, buffer, readSize);
	if(r == NULL){
		return 0;
	}
	ADD_TO_DQUEUE(r, &device->pending);

	pendRWFileIO(rwfr);
	completeRWFileIO(rwfr, readSize, 0);
}
*/
typedef struct{
	I8254xDevice *device;
	Spinlock lock;
}I8254xDeviceList;

static I8254xDeviceList i8254xDeviceList = {NULL, INITIAL_SPINLOCK};

static void addI8254xDeviceList(I8254xDevice *d){
	acquireLock(&i8254xDeviceList.lock);
	ADD_TO_DQUEUE(d, &i8254xDeviceList.device);
	releaseLock(&i8254xDeviceList.lock);
}

static I8254xDevice *searchI8254xDeviceList(int number){
	acquireLock(&i8254xDeviceList.lock);
	I8254xDevice *d;
	for(d = i8254xDeviceList.device; d != NULL; d = d->next){
		if(d->serialNumber == number)
			break;
	}
	releaseLock(&i8254xDeviceList.lock);
	return d;
}


static int openI8254x(OpenFileRequest *ofr, const char *name, uintptr_t nameLength, OpenFileMode mode){
	if(mode.writable){
		//TODO:
		panic("i8254x transmission is not implemented yet");
	}
	if(mode.enumeration){
		panic("not support enum i8254x");
	}
	uintptr_t i;
	for(i = 0; i < nameLength; i++){
		printk("%c", name[i]);
	}
	printk(" %d\n", nameLength);
	int n;
	if(snscanf(name, nameLength, "eth%d", &n) != 1){
		return 0;
	}
	I8254xDevice *device = searchI8254xDeviceList(n);
	if(device == NULL){
		return 0;
	}
	pendOpenFileIO(ofr);
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	//ff.read = readI8254x;
	completeOpenFile(ofr, device, &ff);

	return 0;
}

void i8254xDriver(void){
	uintptr_t pci = enumeratePCI(0x02000000, 0xffffff00);
	if(pci == IO_REQUEST_FAILURE){
		printk("failed to enum PCI");
		systemCall_terminate();
	}
	FileNameFunctions ff = INITIAL_FILE_NAME_FUNCTIONS;
	ff.open = openI8254x;
	if(addFileSystem(&ff, "8254x", strlen("8254x")) == 0){
		printk("failed to create 8254x driver as file\n");
		systemCall_terminate();
	}
	int deviceNumber;
	for(deviceNumber = 0; 1; deviceNumber++){
		PCIConfigRegisters regs;
		if(nextPCIConfigRegisters(pci, &regs, sizeof(regs.regs0)) != sizeof(regs.regs0)){
			break;
		}
		PCIConfigRegisters0 *const regs0 = &regs.regs0;
		int supported = 1;
		if((regs0->bar0 >> 0 & 1) != 0){
			printk("8254x bar0 is not memory mapped io\n");
			supported = 0;
		}
		if(((regs0->bar0 >> 1) & 3) != 0){
			printk("8254x bar0 is not 32 bit\n");
			supported = 0;
		}
		if(supported == 0){
			continue;
		}
		I8254xDevice *i8254x = createI8254xDevice(regs0, deviceNumber);
		if(i8254x == NULL){
			printk("cannot initialize 8254x device");
			continue;
		}
		addI8254xDeviceList(i8254x);
		printk("link status: %x\n", ((i8254x->regs[DEVICE_STATUS] >> 1) & 1));
		PIC *pic = processorLocalPIC();
		addHandler(pic->irqToVector(pic, regs0->interruptLine),i8254xHandler, (uintptr_t)i8254x);
	}
	syncCloseFile(pci);

	while(1)
		sleep(1000);

	systemCall_terminate();
}
