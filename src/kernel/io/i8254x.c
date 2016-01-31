#include"io.h"
#include"task/task.h"
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

typedef struct{
	volatile uint32_t *regs;

	uintptr_t receiveDescCnt; // size is multiple of 128 bytes
	volatile ReceiveDescriptor *receiveDesc; // aligned to 16-byte
	volatile uint8_t (*receiveBuffer)[RECEIVE_DESCRIPTOR_BUFFER_SIZE];

	uintptr_t transmitDescCnt; // size is multiple of 128 bytes
	volatile union{
		ContextTransmitDescriptor context;
		DataTransmitDescriptor data;  // aligned to 16-bit
	}*transmitDesc; //PAGE_SIZE / sizeof(DataTransmitDescriptor);
}I8254xDevice;

static_assert(sizeof(((I8254xDevice*)0)->receiveBuffer[0]) == RECEIVE_DESCRIPTOR_BUFFER_SIZE);

static void initI8254Receive(I8254xDevice* device){
	// set mac address
	// device->regs[RECEIVE_ADDRESS_0_HIGH] =
	// device->regs[RECEIVE_ADDRESS_0_LOW] =
	// clear multicast table array
	uintptr_t i;
	for(i = 0; i < MULTICAST_TABLE_ARRAY_LENGTH; i++){
		device->regs[MULTICAST_TABLE_ARRAY + i] = 0;
	}
	// disable receive delay timer
	device->regs[RECEIVE_DELAY_TIMER] = 0;
	// write 1 to mask to disable interrupt
	device->regs[INTERRUPT_MASK_CLEAR] = 0xffffffff;
	// write 1 to set to enable interrupt
	device->regs[INTERRUPT_MASK_SET_READ] =
		(LINK_STATUS_CHANGE_BIT | RECEIVE_THRESHOLD_REACHED_BIT);
	// descriptor array
	assert(device->receiveDesc != NULL);
	const uintptr_t rdArraySize = device->receiveDescCnt * sizeof(ReceiveDescriptor);
	assert(rdArraySize > 0 && rdArraySize <= PAGE_SIZE && rdArraySize % 128 == 0);
	uint64_t rdPhysical = checkAndTranslatePage(kernelLinear, (void*)device->receiveDesc).value;
	device->regs[RECEIVE_DESCRIPTORS_BASE_LOW] = (rdPhysical & 0xffffffff);
	device->regs[RECEIVE_DESCRIPTORS_BASE_HIGH] = ((rdPhysical >> 32) & 0xffffffff);
	device->regs[RECEIVE_DESCRIPTORS_LENGTH] = rdArraySize;
	// descriptor buffer address
	assert(device->receiveBuffer != NULL);
	for(i = 0; i < device->receiveDescCnt; i++){
		const uintptr_t offset = ((uintptr_t)device->receiveBuffer[i]) % PAGE_SIZE;
		const uintptr_t pageAddress = ((uintptr_t)device->receiveBuffer[i]) - offset;
		PhysicalAddress physicalAddress = checkAndTranslatePage(kernelLinear, (void*)pageAddress);
		MEMSET0((void*)&device->receiveDesc[i]);
		device->receiveDesc[i].address = physicalAddress.value + offset;
		//MEMSET0(device->receiveBuffer[i]);
	}
	device->regs[RECEIVE_DESCRIPTORS_HEAD] = 0;
	device->regs[RECEIVE_DESCRIPTORS_TAIL] = device->receiveDescCnt - 1;
	ReceiveControlRegister rc;
	rc.value = 0;
	// 0 = 1/2 of descriptor array; 1 = 1/4; 2 = 1/8
	// 4096/16/8=32
	rc.receiveDescThreshold = 2;
	//TODO: rc.multicastOffset = ?;
	rc.broadacastAcceptMode = 1;
	//0 = 2048; 1 = 1024; 2 = 512; 3 = 256
	rc.receiveBufferSize = 3;
	//0 = 1 byte; 1 = 16 bytes
	rc.bufferSizeExtension = 0;
	device->regs[RECEIVE_CONTROL] = rc.value;
}

static I8254xDevice *initI8254xDevice(PCIConfigRegisters0 *pciRegs){
	I8254xDevice *NEW(device);
	EXPECT(device != NULL);
	PhysicalAddress pa = {pciRegs->bar0 & 0xfffffff0};
	device->regs = mapKernelPages(pa, I8254X_REGISTERS_SIZE, KERNEL_NON_CACHED_PAGE);
	EXPECT(device->regs != NULL);
	const uintptr_t descArraySize = PAGE_SIZE;
	// 4096 / 16 = 256
	device->receiveDescCnt = descArraySize / sizeof(ReceiveDescriptor);
	device->receiveDesc = allocatePages(kernelLinear, descArraySize, KERNEL_NON_CACHED_PAGE);
	EXPECT(device->receiveDesc != NULL);
	device->transmitDescCnt = descArraySize / sizeof(ContextTransmitDescriptor);
	device->transmitDesc = allocatePages(kernelLinear, descArraySize, KERNEL_NON_CACHED_PAGE);
	EXPECT(device->transmitDesc != NULL);

	// 256 * 256 = 65536
	device->receiveBuffer = allocatePages(kernelLinear,
		sizeof(device->receiveBuffer[0]) * device->receiveDescCnt, KERNEL_NON_CACHED_PAGE);
	EXPECT(device->receiveBuffer != NULL);
	//PhysicalAddress transmitDesc_p = checkAndTranslatePage(kernelLinear, device->transmitDesc);
	initI8254Receive(device);

	printk("%x %x %x\n", device->regs[DEVICE_CONTROL], device->regs[DEVICE_STATUS], device->regs[RECEIVE_CONTROL]);

	return device;
	checkAndReleasePages(kernelLinear, device->receiveBuffer);
	ON_ERROR;
	checkAndReleasePages(kernelLinear, (void*)device->transmitDesc);
	ON_ERROR;
	checkAndReleasePages(kernelLinear, (void*)device->receiveDesc);
	ON_ERROR;
	unmapKernelPages((void*)device->regs);
	ON_ERROR;
	DELETE(device);
	ON_ERROR;
	return NULL;
}

void i8254xDriver(void){
	uintptr_t pci = enumeratePCI(0x02000000, 0xffffff00);
	if(pci == IO_REQUEST_FAILURE){
		printk("enum PCI failed");
		systemCall_terminate();
	}
	while(1){
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
		if(initI8254xDevice(regs0) == NULL){
			printk("cannot initialize 8254x device");
		}
	}
	syncCloseFile(pci);

	while(1)
		sleep(1000);

	systemCall_terminate();
}
