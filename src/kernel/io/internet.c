#include"std.h"
#include"memory/memory.h"
#include"resource/resource.h"
#include"task/task.h"
#include"task/exclusivelock.h"
#include"multiprocessor/processorlocal.h"
#include"file/file.h"

typedef union{
	uint32_t value;
	uint8_t bytes[4];
}IPV4Address;

static_assert(sizeof(IPV4Address) == 4);

// big endian and most significant bit comes first
typedef struct{
	uint8_t headerLength: 4;
	uint8_t version: 4;
	uint8_t congestionNotification: 2;
	uint8_t differentiateService: 6;
	uint16_t totalLength;
	uint16_t identification;
	uint8_t fragmentOffsetHigh: 5;
	uint8_t flags: 3;
	uint8_t fragmentOffsetLow;
	uint8_t timeToLive;
	uint8_t protocol;
	uint16_t headerChecksum;
	IPV4Address source;
	IPV4Address destination;
	//uint8_t options[];
	uint8_t payload[];
}IPV4Header;

/*
example
45 00 00 3c 1a 02 00 00 80 01
2e 6e c0 a8 38 01 c0 a8 38 ff
*/

static_assert(sizeof(IPV4Header) / sizeof(uint32_t) == 5);

static uintptr_t getIPPacketSize(IPV4Header *h){
	return (uintptr_t)changeEndian16(h->totalLength);
}

static uintptr_t getIPHeaderSize(const IPV4Header *h){
	return ((uintptr_t)h->headerLength) * sizeof(uint32_t);
}

// return value in big endian
static uint16_t calculateIPChecksum(const IPV4Header *h){
	uint32_t cs = 0;
	uintptr_t i;
	for(i = 0; i * 2 < getIPHeaderSize(h); i++){
		cs += (uint32_t)changeEndian16(((uint16_t*)h)[i]);
	}
	while((cs & 0xffff) != cs){
		cs = (cs & 0xffff) + (cs >> 16);
	}
	return changeEndian16(cs ^ 0xffff);
}


// TODO: change endian of srcAddress, dstAddress
static void initIPV4Header(IPV4Header *h, uint16_t dataLength, IPV4Address srcAddress, IPV4Address dstAddress){
	h->version = 4;
	h->headerLength = sizeof(IPV4Header) / sizeof(uint32_t);
	h->differentiateService = 0;
	h->congestionNotification = 0;
	h->totalLength = changeEndian16(sizeof(IPV4Header) + dataLength);
	h->identification = changeEndian16(0);
	h->fragmentOffsetHigh = 0;
	h->flags = 2; // 2 = don't fragment; 4 = more fragment
	h->fragmentOffsetLow = 0;
	h->fragmentOffsetHigh = 0;
	h->timeToLive = 32;
	h->protocol = 254; // 1 = ICMP; 6 = TCP; 11 = UDP; 253 = testing; 254 = testing
	h->headerChecksum = 0;
	h->source = srcAddress;
	h->destination = dstAddress;

	h->headerChecksum = calculateIPChecksum(h);
	assert(calculateIPChecksum(h) == 0);
}

static IPV4Header *createIPV4Packet(uint16_t dataLength, IPV4Address srcAddress, IPV4Address dstAddress){
	IPV4Header *h = allocateKernelMemory(sizeof(*h) + dataLength);
	if(h == NULL){
		return NULL;
	}
	initIPV4Header(h, dataLength, srcAddress, dstAddress);
	//memset(h->payload, 0, dataLength);
	return h;
}

static uintptr_t enumNextDataLinkDevice(uintptr_t f, const char *namePattern, FileEnumeration *fe){
	return enumNextResource(f, fe, (uintptr_t)namePattern, matchWildcardName);
}

typedef struct{
	Semaphore *argCount;
	Spinlock lock;
	struct RWIPArgument *argList;
	uintptr_t fileHandle;
	uintptr_t mtu;
	Task *transmitIPTask;
}IPTransmitter;

static void transmitIPTask(void *arg);

static int initIPTransmitter(IPTransmitter *t, uintptr_t fileHandle, uintptr_t mtu){
	t->argCount = createSemaphore(0);
	EXPECT(t->argCount != NULL);
	t->lock = initialSpinlock;
	t->argList = NULL;
	t->fileHandle = fileHandle;
	t->mtu = mtu;
	t->transmitIPTask = createSharedMemoryTask(transmitIPTask, &t, sizeof(&t), processorLocalTask());
	EXPECT(t->transmitIPTask != NULL);
	resume(t->transmitIPTask);
	return 1;
	// terminate task
	ON_ERROR;
	deleteSemaphore(t->argCount);
	ON_ERROR;
	return 0;
}

typedef struct DataLinkDevice{
	IPV4Address bindingAddress;
	IPV4Address subnetMask;
	IPTransmitter transmitter;
	//receiver
	struct DataLinkDevice **prev, *next;
}DataLinkDevice;

typedef struct{
	DataLinkDevice *head;
	Spinlock lock;
}DataLinkDeviceList;
DataLinkDeviceList dataLinkDevList = {NULL, INITIAL_SPINLOCK};

static DataLinkDevice *createDataLinkDevice(
	const char *fileName, uintptr_t nameLength,
	IPV4Address address, IPV4Address subnetMask
){
	DataLinkDevice *NEW(d);
	EXPECT(d != NULL);

	OpenFileMode ofm = OPEN_FILE_MODE_0;
	ofm.writable = 1;
	uintptr_t f = syncOpenFileN(fileName, nameLength, ofm);
	EXPECT(f != IO_REQUEST_FAILURE);

	uintptr_t mtu;
	uintptr_t r = syncWritableSizeOfFile(f, &mtu);
	EXPECT(r != IO_REQUEST_FAILURE);

	int ok = initIPTransmitter(&d->transmitter, f, mtu);
	EXPECT(ok);
	//ok = initIPReceiver(&d->receiver);
	//EXPECT(ok)
	d->bindingAddress = address;
	d->subnetMask = subnetMask;
	d->prev = NULL;
	d->next = NULL;
	return d;
	// terminate transmit task
	ON_ERROR;
	// mtu
	ON_ERROR;
	syncCloseFile(f);
	ON_ERROR;
	DELETE(d);
	ON_ERROR;
	return NULL;
}
/*
static void deleteDataLinkDevice(DataLinkDevice *d){
	syncCloseFile(d->fileHandle);
	assert(IS_IN_DQUEUE(d) == 0);
	DELETE(d);
}
*/
static void addDataLinkDevice(DataLinkDeviceList *dlList, DataLinkDevice *d){
	acquireLock(&dlList->lock);
	ADD_TO_DQUEUE(d, &dlList->head);
	releaseLock(&dlList->lock);
}

// find the first data link device whose subnet mask matches address
static DataLinkDevice *searchDeviceByIP_noLock(DataLinkDeviceList *dlList, IPV4Address address){
	assert(isAcquirable(&dlList->lock) == 0);
	DataLinkDevice *d;
	for(d = dlList->head; d != NULL; d = d->next){
		if(d->bindingAddress.value == (d->subnetMask.value & address.value)){
			break;
		}
	}
	return d;
}

typedef struct{
	IPV4Address source;
	IPV4Address destination;
}IPSocket;

static int setIPAddress(FileIORequest2 *fior2, OpenedFile *of, uintptr_t param, uint64_t value){
	IPSocket *ips = getFileInstance(of);
	switch(param){
	case FILE_PARAM_SOURCE_ADDRESS:
		ips->source = (IPV4Address)(uint32_t)value;
		break;
	case FILE_PARAM_DESTINATION_ADDRESS:
		ips->destination = (IPV4Address)(uint32_t)value;
		break;
	default:
		return 0;
	}
	completeFileIO0(fior2);
	return 1;
}

typedef struct RWIPArgument{
	RWFileRequest *rwfr;
	IPSocket *ipSocket;
	uint8_t *buffer;
	uintptr_t size;

	struct RWIPArgument **prev, *next;
}RWIPArgument;

static int writeIPSocket(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){
	IPSocket *ips = getFileInstance(of);
	// find device
	DataLinkDeviceList *dlList = &dataLinkDevList;
	acquireLock(&dlList->lock);
	DataLinkDevice *dld = searchDeviceByIP_noLock(dlList, ips->source);
	releaseLock(&dlList->lock);
	if(dld == NULL){
		return 0;
	}
	// create RWIPRequest
	RWIPArgument *NEW(arg);
	if(arg == NULL){
		return 0;
	}
	arg->rwfr = rwfr;
	arg->ipSocket = ips;
	arg->buffer = (uint8_t*)buffer;
	arg->size = size;
	arg->prev = NULL;
	arg->next = NULL;
	// add to request list
	acquireLock(&dld->transmitter.lock);
	ADD_TO_DQUEUE(arg, &dld->transmitter.argList);
	releaseLock(&dld->transmitter.lock);
	releaseSemaphore(dld->transmitter.argCount);
	return 1;
}

static int transmitIP(IPTransmitter *tran, const RWIPArgument *arg){
	EXPECT(arg->size <= tran->mtu - sizeof(IPV4Header));
	IPV4Header *packet = createIPV4Packet(arg->size, arg->ipSocket->source, arg->ipSocket->destination);
	EXPECT(packet != NULL);
	memcpy(packet->payload, arg->buffer, arg->size);
	uintptr_t writeSize = getIPPacketSize(packet);
	uintptr_t r = syncWriteFile(tran->fileHandle, packet, &writeSize);
	EXPECT(r != IO_REQUEST_FAILURE && writeSize == getIPPacketSize(packet));
	DELETE(packet);
	return 1;
	// cancel write
	ON_ERROR;
	DELETE(packet);
	ON_ERROR;
	// larger than MTU
	ON_ERROR;
	return 0;
}

static void transmitIPTask(void *voidArg){
	IPTransmitter *tran = *(IPTransmitter**)voidArg;
	while(1){
		acquireSemaphore(tran->argCount);
		acquireLock(&tran->lock);
		RWIPArgument *arg = tran->argList;
		assert(arg != NULL);
		REMOVE_FROM_DQUEUE(arg);
		releaseLock(&tran->lock);

		int ok = transmitIP(tran, arg);
		completeRWFileIO(arg->rwfr, (ok? arg->size: 0), 0);
		DELETE(arg); // see writeIPSocket
	}
	systemCall_terminate();
}

static void closeIPSocket(CloseFileRequest *cfr, OpenedFile *of){
	IPSocket *ips = getFileInstance(of);
	completeCloseFile(cfr);
	DELETE(ips);
}

static int openIPSocket(
	OpenFileRequest *ofr,
	__attribute__((__unused__)) const char *fileName, __attribute__((__unused__)) uintptr_t nameLength,
	OpenFileMode ofm
){
	// TODO: check filename
	EXPECT(ofm.writable);
	IPSocket *NEW(socket);
	EXPECT(socket != NULL);
	IPV4Address defaultAddress = {bytes: {127, 0, 0, 1}};
	socket->source = defaultAddress;
	socket->destination = defaultAddress;
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.setParameter = setIPAddress;
	//ff.read = TODO;
	ff.write = writeIPSocket;
	ff.close = closeIPSocket;
	completeOpenFile(ofr, socket, &ff);
	return 1;
	ON_ERROR;
	ON_ERROR;
	return 0;
}

/* TODO:
static int testReadIPPacket(uintptr_t fileHandle){
	IPV4Header *h = createIPV4Packet(100, (IPV4Address)(uint32_t)1234, (IPV4Address)(uint32_t)4567);
	if(h == NULL){
		printk("cannot create ip packet");
		goto testReadFail;
	}
	uintptr_t readSize = getIPPacketSize(h);
	uintptr_t r = syncReadFile(fileHandle, h, &readSize);
	if(r == IO_REQUEST_FAILURE || readSize < sizeof(*h)){
		printk("read failed %x bad IP packet size %d\n", r, readSize);
		goto testReadFail;
	}
	if(calculateIPChecksum(h) != 0){
		printk("bad IP checksum\n");
		goto testReadFail;
	}
	printk("%d.%d.%d.%d\n", h->source.bytes[0], h->source.bytes[1], h->source.bytes[2], h->source.bytes[3]);
	printk("%d.%d.%d.%d\n", h->destination.bytes[0], h->destination.bytes[1], h->destination.bytes[2], h->destination.bytes[3]);
	uintptr_t i;
	for(i = 0; i < sizeof(*h); i++){
		printk(" %x%x", (((uint8_t*)h)[i] & 0xf0) / 16, ((uint8_t*)h)[i] & 0x0f);
		if(i % 10 == 9)printk("\n");
	}
	return 1;
	testReadFail:
	DELETE(h);
	return 0;
}
*/
void internetService(void){
	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openIPSocket;
	addFileSystem(&fnf, "ip", strlen("ip"));
	//const char *testSource = "192.168.1.10";
	uintptr_t enumDataLink = syncEnumerateFile(resourceTypeToFileName(RESOURCE_DATA_LINK_DEVICE));
	while(1){
		FileEnumeration fe;
		uintptr_t r = enumNextDataLinkDevice(enumDataLink, "*", &fe);
		if(r == IO_REQUEST_FAILURE){
			printk("cannot enum next data link device\n");
			assert(0);
			continue;
		}
		IPV4Address defaultAddress = {bytes: {0, 0, 0, 0}};
		IPV4Address defaultSubnetMask = {bytes: {255, 255, 255, 255}};
		DataLinkDevice *d = createDataLinkDevice(fe.name, fe.nameLength, defaultAddress, defaultSubnetMask);
		addDataLinkDevice(&dataLinkDevList, d);
	}
	syncCloseFile(enumDataLink);
	systemCall_terminate();
}


#ifndef NDEBUG

void testWriteIP(void);
void testWriteIP(void){
	int ok = waitForFirstResource("ip", RESOURCE_FILE_SYSTEM, matchName);
	// wait for device
	sleep(500);
	assert(ok);
	OpenFileMode ofm = OPEN_FILE_MODE_0;
	ofm.writable = 1;
	uintptr_t f = syncOpenFileN("ip:", strlen("ip:"), ofm);
	assert(f != IO_REQUEST_FAILURE);
	IPV4Address src0 = {bytes: {0, 0, 0, 0}};
	IPV4Address src1 = {bytes: {0, 0, 0, 1}};
	uintptr_t r, i;
	for(i = 0; i < 30; i++){
		uint8_t buffer[20];
		memset(buffer, i + 10, sizeof(buffer));
		uintptr_t writeSize = sizeof(buffer);

		r = syncSetFileParameter(f, FILE_PARAM_SOURCE_ADDRESS, src0.value);
		assert(r != IO_REQUEST_FAILURE);
		r = syncWriteFile(f, buffer, &writeSize);
		assert(r != IO_REQUEST_FAILURE && writeSize == sizeof(buffer));

		r = syncSetFileParameter(f, FILE_PARAM_SOURCE_ADDRESS, src1.value);
		assert(r != IO_REQUEST_FAILURE);
		r = syncWriteFile(f, buffer, &writeSize);
		assert(r == IO_REQUEST_FAILURE);
	}
	r = syncCloseFile(f);
	assert(r != IO_REQUEST_FAILURE);
	printk("test write IP ok\n");
	systemCall_terminate();
}
#endif
