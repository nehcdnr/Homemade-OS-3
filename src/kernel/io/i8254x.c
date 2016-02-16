#include"io.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/controller/pic.h"
#include"task/exclusivelock.h"
#include"file/file.h"

typedef struct{
	uint8_t reserved0[12];
	uint8_t status;
	uint8_t reserved1[3];
}GenericDescriptor;

static_assert(sizeof(GenericDescriptor) == 16);

typedef struct{
	uint64_t address;
	uint16_t length;
	uint16_t checksum; // not supported in some chips
	uint8_t status;
	uint8_t errors;
	uint16_t special; // not support in some chips
}ReceiveDescriptor;

static_assert(sizeof(ReceiveDescriptor) == 16);

static void initReceiveDescriptor(volatile ReceiveDescriptor *r, volatile void *buffer){
	const uintptr_t offset = ((uintptr_t)buffer) % PAGE_SIZE;
	const uintptr_t pageAddress = ((uintptr_t)buffer) - offset;
	PhysicalAddress physicalAddress = checkAndTranslatePage(kernelLinear, (void*)pageAddress);
	assert(physicalAddress.value != INVALID_PAGE_ADDRESS);
	memset((void*)r, 0 ,sizeof(*r));
	r->address = physicalAddress.value + offset;
}

typedef struct{
	uint64_t address;
	uint16_t length;
	uint8_t checksumOffset;
	uint8_t command;
	uint8_t status: 4;
	uint8_t reserved: 4;
	uint8_t checksumStart;
	uint16_t special;
}LegacyTransmitDescriptor;

static_assert(sizeof(LegacyTransmitDescriptor) == 16);

static int initTransmitDescriptor(volatile LegacyTransmitDescriptor *td, volatile void *buffer, uintptr_t length){
	union TransmitCommand{
		uint8_t value;
		struct{
			uint8_t endOfPacket: 1;
			uint8_t insertFCS: 1;
			uint8_t insertChecksum: 1;
			uint8_t reportStatus: 1;
			uint8_t reportPacketSent: 1;
			uint8_t extension: 1;
			uint8_t vlanEnable: 1;
			uintptr_t delayInterrupt: 1;
		};
	};
	if(length > 16288 || length < 48){
		return 0;
	}
	uintptr_t offset = ((uintptr_t)buffer) % PAGE_SIZE;
	PhysicalAddress pa = checkAndTranslatePage(kernelLinear, (void*)(((uintptr_t)buffer) - offset));
	if(pa.value == INVALID_PAGE_ADDRESS){
		return 0;
	}
	union TransmitCommand cmd = {value: 0};
	cmd.endOfPacket = 1; // TODO: test
	cmd.insertFCS = 1;
	cmd.insertChecksum = 0;
	cmd.reportStatus = 1; // TODO: test
	cmd.reportPacketSent = 0;
	cmd.extension = 0;
	cmd.vlanEnable = 0;
	cmd.delayInterrupt = 1;
	td->address = pa.value + offset;
	td->length = length;
	td->checksumOffset = 0;
	td->checksumStart = 0;
	td->command = cmd.value;
	td->reserved = 0;
	td->status = 0;
	td->special = 0;
	return 1;
}

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
	uint32_t command: 8;
	uint8_t status: 4;
	uint8_t reserved: 4;
	uint8_t packetOption;
	uint16_t special;
}DataTransmitDescriptor;

static_assert(sizeof(DataTransmitDescriptor) == 16);

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
	RECEIVE_SMALL_PACKET_SIZE = 0x2c00 / S,

	TRANSMIT_CONTROL = 0x400 / S,
	TRANSMIT_PACKET_GAP = 0x410 / S,
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
	RECEIVE_SEQUENCE_ERROR_BIT = (1 << 3),
	RECEIVE_THRESHOLD_REACHED_BIT = (1 << 4),
	//reserve (1 << 5)
	RECEIVER_OVERRUN_BIT = (1 << 6),
	RECEIVER_TIMER_EXPIRE_BIT = (1 << 7),
	RECEIVER_SMALL_PACKET_BIT = (1 << 16)
};

#define RECEIVE_INTERRUPT_BITS \
	(RECEIVE_SEQUENCE_ERROR_BIT | RECEIVE_THRESHOLD_REACHED_BIT | \
	RECEIVER_OVERRUN_BIT | RECEIVER_TIMER_EXPIRE_BIT | RECEIVER_SMALL_PACKET_BIT)

#define TRANSMIT_INTERRUPT_BITS \
	(TRANSMIT_DESC_WRITTEN_BACK_BIT | TRANSMIT_QUEUE_EMPTY_BIT)
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

typedef union{
	uint32_t value;
	struct{
		uint32_t reserved0: 1;
		uint32_t enabled: 1;
		uint32_t reserved1: 1;
		uint32_t padShortPacket: 1;
		uint32_t collisionThreshold: 8;
		uint32_t collisionDistance: 10;
		uint32_t softwareXOFF: 1;
		uint32_t reserved2: 1;
		uint32_t retransmitLastCollision: 1;
		uint32_t noRetransmitUnderrun: 1;
		uint32_t reserved3: 6;
	};
}TransmitControlRegister;

typedef union{
	uint32_t value;
	struct{
		uint32_t transmitTime: 10;
		uint32_t receiveTime1: 10;
		uint32_t receiveTime2: 10;
		uint32_t reserved: 2;
	};
}InterPacketGapRegister;

static_assert(sizeof(TransmitControlRegister) == sizeof(uint32_t));

typedef struct RWI8254xRequest{
	RWFileRequest *rwfr;
	uint8_t *buffer;
	uintptr_t rwSize;

	Spinlock *lock;
	struct RWI8254xRequest **prev, *next;
}RWI8254xRequest;

static RWI8254xRequest *createRWI8254xRequest(RWFileRequest *rwfr, uint8_t *buffer, uintptr_t rwSize){
	RWI8254xRequest *NEW(r);
	if(r == NULL){
		return NULL;
	}
	r->rwfr = rwfr;
	r->buffer = buffer;
	r->rwSize = rwSize;
	r->lock = NULL;
	r->prev = NULL;
	r->next = NULL;
	return r;
}

#define RECEIVE_DESCRIPTOR_BUFFER_SIZE (256)

typedef struct{
	uintptr_t descriptorCount; // size is multiple of 128 bytes
	union{
		volatile GenericDescriptor *generic;
		volatile LegacyTransmitDescriptor *legacy;
		volatile ContextTransmitDescriptor *context;
		volatile DataTransmitDescriptor *data;
		volatile ReceiveDescriptor *receive;
	}; // aligned to 16-bit
	uintptr_t bufferSize;
	volatile uint8_t *bufferArray;

	// HEAD >= intHead >= taskHead > taskTail >= TAIL
	uintptr_t intHead, taskHead, taskTail;
	Semaphore *intSemaphore;
}I8254xDescriptorQueue;

typedef struct{
	I8254xDescriptorQueue queue;

	Spinlock lock;
	RWI8254xRequest *pending, *serving;
	Semaphore *reqSemaphore;
}I8254xTransmit;

typedef struct{
	I8254xDescriptorQueue queue;
}I8254xReceive;

static void addPendingRWI8254xRequest(I8254xTransmit *q, RWI8254xRequest *r){
	assert(r->lock == NULL);
	r->lock = &q->lock;
	acquireLock(r->lock);
	ADD_TO_DQUEUE(r, &q->pending);
	releaseLock(r->lock);
}
/*
static void removeRWI8254xRequest(RWI8254xRequest *r){
	assert(r->lock != NULL);
	acquireLock(r->lock);
	REMOVE_FROM_DQUEUE(r);
	releaseLock(r->lock);
	r->lock = NULL;
}
*/
#define MAC_ADDRESS_SIZE (6)
#define BROADCAST_MAC_ADDRESS ((((uint64_t)0xffff) << 32) | 0xffffffff)


#define TO_BIG_ENDIAN_16(X) ((uint16_t)((((X) << 16) & 0xff00) | (((X) >> 16) & 0xff)))

#define ETHERTYPE_IPV4 TO_BIG_ENDIAN_16(0x0800)
#define ETHERTYPE_ARP TO_BIG_ENDIAN_16(0x0806)

typedef struct{
	// big endian
	uint8_t dstMACAddress[MAC_ADDRESS_SIZE];
	uint8_t srcMACAddress[MAC_ADDRESS_SIZE];
	// IPv4 = 0x0800; ARP = 0x0806 in big endian
	uint16_t etherType;
	uint8_t payload[0];
}EthernetHeader;

static void toMACAddress(volatile uint8_t *outAddress, uint64_t macAddress){
	int a;
	for(a = 0; a < MAC_ADDRESS_SIZE; a++){
		outAddress[a] = ((macAddress >> (a * 8)) & 0xff);
	}
}

static_assert(sizeof(EthernetHeader) == 14);

typedef struct I8254xDevice{
	volatile uint32_t *regs;

	int terminateFlag;
	I8254xTransmit transmit;
	Task *transmitTask;
	I8254xReceive receive;
	Task *receiveTask;

	int serialNumber;

	struct I8254xDevice *next, **prev;
}I8254xDevice;

static int initDescriptorQueue(I8254xDescriptorQueue *r){
	const uintptr_t descArraySize = PAGE_SIZE;
	// 4096 / 16 = 256
	r->descriptorCount = descArraySize / sizeof(r->generic[0]);
	r->receive = allocatePages(kernelLinear, descArraySize, KERNEL_NON_CACHED_PAGE);
	EXPECT(r->receive != NULL);
	// 256 * 256 = 65536
	r->bufferSize = RECEIVE_DESCRIPTOR_BUFFER_SIZE;
	r->bufferArray = allocatePages(kernelLinear, r->bufferSize * r->descriptorCount, KERNEL_NON_CACHED_PAGE);
	EXPECT(r->bufferArray != NULL);
	// see regs[RECEIVE_DESCRIPTORS_HEAD] or regs[TRANSMIT_DESCRIPTORS_HEAD]
	r->intHead = 0;
	r->taskHead = 0;
	r->taskTail = 0;
	r->intSemaphore = createSemaphore(0);
	EXPECT(r->intSemaphore != NULL);

	return 1;
	//deleteSemaphore(r->intSemaphore);
	ON_ERROR;
	checkAndReleasePages(kernelLinear, (void*)r->bufferArray);
	ON_ERROR;
	checkAndReleasePages(kernelLinear, (void*)r->receive);
	ON_ERROR;
	return 0;
}

static uint64_t getDescriptorQueueBase(I8254xDescriptorQueue *q){
	uint64_t p = checkAndTranslatePage(kernelLinear, (void*)q->legacy).value;
	assert((uintptr_t)p != INVALID_PAGE_ADDRESS);
	return p;
}
static void destroyDescriptorQueue(I8254xDescriptorQueue *q){
	// reset register
	assert(getSemaphoreValue(q->intSemaphore) == 0 &&
		q->bufferArray != NULL && q->receive != NULL);
	deleteSemaphore(q->intSemaphore);
	checkAndReleasePages(kernelLinear, (void*)q->bufferArray);
	checkAndReleasePages(kernelLinear, (void*)q->receive);
}

static int initI8254Receive(I8254xReceive *r, volatile uint32_t *regs){
	I8254xDescriptorQueue *q = &r->queue;
	int ok = initDescriptorQueue(q);
	EXPECT(ok);
	// set mac address
	// regs[RECEIVE_ADDRESS_0_HIGH] =
	// regs[RECEIVE_ADDRESS_0_LOW] =
	// printk("%x %x\n", regs[RECEIVE_ADDRESS_0_HIGH], regs[RECEIVE_ADDRESS_0_LOW]);
	// clear multicast table array
	uintptr_t i;
	for(i = 0; i < MULTICAST_TABLE_ARRAY_LENGTH; i++){
		regs[MULTICAST_TABLE_ARRAY + i] = 0;
	}
	// disable receive delay timer
	regs[RECEIVE_DELAY_TIMER] = 0;
	// if packet size <= threshold, interrupt immediately
	regs[RECEIVE_SMALL_PACKET_SIZE] = 0;
	// iniI8254xTransmit also sets LINK_STATUS
	// write 1 to disable interrupt
	regs[INTERRUPT_MASK_CLEAR] = (LINK_STATUS_CHANGE_BIT | RECEIVE_INTERRUPT_BITS);
	// write 1 to enable interrupt
	regs[INTERRUPT_MASK_SET_READ] = (LINK_STATUS_CHANGE_BIT | RECEIVE_INTERRUPT_BITS);
	// descriptor array
	const uintptr_t descArraySize = q->descriptorCount * sizeof(q->receive[0]);
	uint64_t rdPhysical = getDescriptorQueueBase(q);
	assert(descArraySize > 0 && descArraySize <= PAGE_SIZE && descArraySize % 128 == 0);
	regs[RECEIVE_DESCRIPTORS_BASE_LOW] = LOW64(rdPhysical);
	regs[RECEIVE_DESCRIPTORS_BASE_HIGH] = HIGH64(rdPhysical);
	regs[RECEIVE_DESCRIPTORS_LENGTH] = descArraySize;
	// descriptor buffer address
	for(i = 0; i < q->descriptorCount; i++){
		initReceiveDescriptor(&q->receive[i], q->bufferArray + i * q->bufferSize);
	}
	q->taskTail = q->descriptorCount - 1;
	regs[RECEIVE_DESCRIPTORS_HEAD] = 0;
	regs[RECEIVE_DESCRIPTORS_TAIL] = q->descriptorCount - 1;
	ReceiveControlRegister rc;
	rc.value = 0;
	rc.enabled = 1;
	// do not filter destination address
	// rc.unicastPromiscuous = 0;
	// rc.multicastPromiscuous = 0;
	// 0 = 1/2 of descriptor array; 1 = 1/4; 2 = 1/8
	// 4096/16/8=32
	rc.receiveDescThreshold = 2;
	// bit 36~48 as lookup address
	// IMPROVE: rc.multicastOffset = 0
	// accept broadcast
	rc.broadacastAcceptMode = 1;
	// 0 = 2048; 1 = 1024; 2 = 512; 3 = 256
	switch(q->bufferSize){
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
	default:
		assert(0);
	}
	//0 = 1 byte; 1 = 16 bytes
	rc.bufferSizeExtension = (q->bufferSize >= 4096? 1: 0);
	rc.stripEthernetCRC = 1;
	regs[RECEIVE_CONTROL] = rc.value;

	return 1;
	//destroyDescriptorQueue(q);
	ON_ERROR;
	return 0;
}

static void destroyI8254xReceive(I8254xReceive *r){
	destroyDescriptorQueue(&r->queue);
}

static int initI8254xTransmit(I8254xTransmit *t, volatile uint32_t *regs){
	I8254xDescriptorQueue *q = &t->queue;
	int ok = initDescriptorQueue(q);
	EXPECT(ok);
	t->lock = initialSpinlock;
	t->pending = NULL;
	t->serving = NULL;
	t->reqSemaphore = createSemaphore(0);
	EXPECT(t->reqSemaphore != NULL);

	// iniI8254xReceive also sets LINK_STATUS
	regs[INTERRUPT_MASK_CLEAR] |=
		(TRANSMIT_INTERRUPT_BITS | LINK_STATUS_CHANGE_BIT);
	regs[INTERRUPT_MASK_SET_READ] |=
		(TRANSMIT_INTERRUPT_BITS | LINK_STATUS_CHANGE_BIT);
	// descriptor array
	const uintptr_t tdArraySize = q->descriptorCount * sizeof(q->legacy[0]);
	uint64_t tdAddress = checkAndTranslatePage(kernelLinear, (void*)q->legacy).value;
	regs[TRANSMIT_DESCRIPTORS_BASE_LOW] = LOW64(tdAddress);
	regs[TRANSMIT_DESCRIPTORS_BASE_HIGH] = HIGH64(tdAddress);
	regs[TRANSMIT_DESCRIPTORS_LENGTH] = tdArraySize;
	// TAIL status must be 0. see i8254xTransmitHandler
	q->legacy[0].status = 0;
	regs[TRANSMIT_DESCRIPTORS_HEAD] = 0;
	regs[TRANSMIT_DESCRIPTORS_TAIL] = 0;

	// 100Mbps = 12.5 byte/usec
	// 1000 * 12.5 = 12500
	regs[TRANSMIT_DELAY_TIMER] = 1000; // in 1.024 usec

	InterPacketGapRegister ipg = {value: 0};
	ipg.transmitTime = 10;
	ipg.receiveTime1 = 10;
	ipg.receiveTime2 = 10;
	regs[TRANSMIT_PACKET_GAP] = ipg.value;

	TransmitControlRegister tc = {value: 0};
	tc.enabled = 1;
	tc.padShortPacket = 1;
	tc.collisionThreshold = 0x10;
	tc.collisionDistance = 0x40;
	regs[TRANSMIT_CONTROL] = tc.value;
	return 1;
	deleteSemaphore(t->reqSemaphore);
	ON_ERROR;
	destroyDescriptorQueue(q);
	ON_ERROR;
	return 0;
}

static void destroyI8254xTransmit(I8254xTransmit *tran){
	assert(getSemaphoreValue(tran->reqSemaphore) == 0);
	deleteSemaphore(tran->reqSemaphore);
	destroyDescriptorQueue(&tran->queue);
}

static void i8254xReceiveTask(void *arg){
	I8254xDevice *d = *(I8254xDevice**)arg;
	I8254xReceive *r = &d->receive;
	I8254xDescriptorQueue *q = &r->queue;

	printk("receiver %d started\n", d->serialNumber);
	while(1){
		acquireSemaphore(q->intSemaphore);
		const uintptr_t head = q->taskHead;
		//unsigned a;
		//for(a = 0; a < r->receiveDesc[head].length && a<3; a++){
		//	printk("%c", r->receiveBuffer[head][a]);
		//}
		printk("received:%d bytes\n", (int)q->receive[head].length);
		q->taskHead = (head + 1) % q->descriptorCount;

		assert(q->receive[q->taskTail].status == 0);
		q->taskTail = (q->taskTail + 1) % q->descriptorCount;
		const uintptr_t tail = q->taskTail;

		//memset((void*)&r->receiveDesc[t], 0 ,sizeof(r->receiveDesc[t]));
		//r->receiveDesc[t].buffer =
		q->receive[tail].status = 0;
		d->regs[RECEIVE_DESCRIPTORS_TAIL] = tail;
		assert(tail == d->regs[RECEIVE_DESCRIPTORS_TAIL]);
	}
	systemCall_terminate();
}

static void i8254xTransmitTask(void *arg){
	I8254xDevice *d = *(I8254xDevice**)arg;
	I8254xTransmit *t = &d->transmit;
	I8254xDescriptorQueue *q = &t->queue;

	if(((d->regs[RECEIVE_ADDRESS_0_HIGH] >> 31) & 1) == 0){
		assert(0);
	}
	uint64_t srcMAC = COMBINE64(d->regs[RECEIVE_ADDRESS_0_HIGH] & 0xffff, d->regs[RECEIVE_ADDRESS_0_LOW]);
	printk("transmitter started %d\n", d->serialNumber);
	// TODO: this is test
	sleep(500);
	unsigned i;
	for(i = 0; d->serialNumber == 0 && i < 4; i++){
		//sleep(); // TODO: wait for rw file request
		const uintptr_t tail = q->taskTail;
		q->taskTail = (tail + 1) % q->descriptorCount;
		q->legacy[q->taskTail].status = 0;
		// TODO: process rw file request
		const uintptr_t payloadSize = 60;
		volatile EthernetHeader *h = (volatile EthernetHeader*)(q->bufferArray + tail * q->bufferSize);
		toMACAddress(h->dstMACAddress, BROADCAST_MAC_ADDRESS);
		toMACAddress(h->srcMACAddress, srcMAC);
		h->etherType = ETHERTYPE_IPV4;
		unsigned j;
		for(j = 0; j < payloadSize; j++){
			h->payload[j] = i + '0';
		}
		if(initTransmitDescriptor(&q->legacy[tail], h, sizeof(*h) + payloadSize) == 0){
			printk("init transmit desc error\n");
		}
		assert(d->regs[TRANSMIT_DESCRIPTORS_TAIL] == tail);
		d->regs[TRANSMIT_DESCRIPTORS_TAIL] = (tail + 1) % q->descriptorCount;
		// wait for interrupt
		acquireSemaphore(q->intSemaphore);

		// TODO: complete write file
		q->taskHead = (q->taskHead + 1) % q->descriptorCount;
		assert(q->legacy[q->taskTail].status == 0);
	}
	printk("test transmit ok\n");
	acquireSemaphore(q->intSemaphore);
	panic("transmit semaphore");
	systemCall_terminate();
}

static I8254xDevice *createI8254xDevice(PCIConfigRegisters0 *pciRegs, int number){
	I8254xDevice *NEW(device);
	EXPECT(device != NULL);
	device->serialNumber = number;
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

	return device;
	ON_ERROR;
	device->terminateFlag = 1;
	// TODO: kill and wait tasks
	ON_ERROR;
	device->regs[DEVICE_CONTROL] = (1 << 26); // trigger reset
	//while(device->regs[DEVICE_CONTROL] & (1 << 26)); poll until the bit is cleared
	destroyI8254xTransmit(&device->transmit);
	ON_ERROR;
	destroyI8254xReceive(&device->receive);
	ON_ERROR;
	unmapKernelPages((void*)device->regs);
	ON_ERROR;
	DELETE(device);
	ON_ERROR;
	return NULL;
}

static int descriptorQueueHandler(I8254xDescriptorQueue *q, uint32_t regsHead){
	int handled = 0, reachedHead = 0;
	while(1){
		if(q->intHead == regsHead)
			reachedHead = 1;
		// until the first not-done descriptor after HEAD
		if(reachedHead && (q->generic[q->intHead].status & 1) == 0)
			break;
		q->intHead = (q->intHead + 1) % q->descriptorCount;
		// send to receiver or transmit service
		releaseSemaphore(q->intSemaphore);
		handled = 1;
	}
	return handled;
}

static int i8254xHandler(const InterruptParam *p){
	I8254xDevice *i8254x = (I8254xDevice*)p->argument;
	// reading the register implicitly clears interrupt status
	uint32_t cause = i8254x->regs[INTERRUPT_CAUSE_READ];
	int handled = (cause != 0);
	if(cause & LINK_STATUS_CHANGE_BIT){
		printk("link status change: %x\n", ((i8254x->regs[DEVICE_STATUS] >> 1) & 1));
	}
	if(cause & RECEIVE_INTERRUPT_BITS){
		descriptorQueueHandler(&i8254x->receive.queue,  i8254x->regs[RECEIVE_DESCRIPTORS_HEAD]);
	}
	if(cause & TRANSMIT_INTERRUPT_BITS){
		descriptorQueueHandler(&i8254x->transmit.queue, i8254x->regs[TRANSMIT_DESCRIPTORS_TAIL]);
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

	completeRWFileIO(rwfr, readSize, 0);
}
*/
static int writeI8254x(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t writeSize){
	I8254xDevice *device = getFileInstance(of);
	RWI8254xRequest *w = createRWI8254xRequest(rwfr, (uint8_t *)buffer, writeSize);
	EXPECT(w != NULL);
	addPendingRWI8254xRequest(&device->transmit, w);

	return 1;

	ON_ERROR;
	return 0;
}

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
		panic("not implemented");
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
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	//ff.read = readI8254x;
	ff.write = writeI8254x;
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
		PIC *pic = processorLocalPIC();
		addHandler(pic->irqToVector(pic, regs0->interruptLine),i8254xHandler, (uintptr_t)i8254x);
		pic->setPICMask(pic, regs0->interruptLine, 0);
		// set link up
		i8254x->regs[DEVICE_CONTROL] |= (1 << 6);
		printk("link status: %x\n", ((i8254x->regs[DEVICE_STATUS] >> 1) & 1));
	}
	syncCloseFile(pci);

	while(1){
		sleep(1000);
	}

	systemCall_terminate();
}
