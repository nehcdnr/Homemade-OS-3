#include"std.h"
#include"memory/memory.h"
#include"resource/resource.h"
#include"task/task.h"
#include"task/exclusivelock.h"
#include"multiprocessor/processorlocal.h"
#include"file/file.h"
#include"network.h"

static_assert(sizeof(IPV4Address) == 4);
static_assert(sizeof(IPV4Header) / sizeof(uint32_t) == 5);

static uintptr_t getIPPacketSize(const IPV4Header *h){
	return (uintptr_t)changeEndian16(h->totalLength);
}

uintptr_t getIPHeaderSize(const IPV4Header *h){
	return ((uintptr_t)h->headerLength) * sizeof(uint32_t);
}

uintptr_t getIPDataSize(const IPV4Header *h){
	return getIPPacketSize(h) - getIPHeaderSize(h);
}

// return big endian number
static uint16_t calculateIPChecksum(const IPV4Header *h){
	uint32_t cs = 0;
	uintptr_t i;
	uintptr_t headerSize = getIPHeaderSize(h);
	for(i = 0; i * 2  + 1 < headerSize; i++){
		cs += (uint32_t)changeEndian16(((uint16_t*)h)[i]);
	}
	while(cs > 0xffff){
		cs = (cs & 0xffff) + (cs >> 16);
	}
	return changeEndian16(cs ^ 0xffff);
}

uint32_t calculatePseudoIPChecksum(const IPV4Header *h){
	uint32_t cs = 0;
	cs += changeEndian16(h->source.value & 0xffff);
	cs += changeEndian16(h->source.value >> 16);
	cs += changeEndian16(h->destination.value & 0xffff);
	cs += changeEndian16(h->destination.value >> 16);
	cs += h->protocol;
	cs += getIPDataSize(h);
	return cs;
}

void initIPV4Header(
	IPV4Header *h, uint16_t dataLength, IPV4Address srcAddress, IPV4Address dstAddress,
	enum IPDataProtocol dataProtocol
){
	h->version = 4;
	// no optional entries
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
	h->protocol = dataProtocol;
	h->headerChecksum = 0;
	h->source = srcAddress;
	h->destination = dstAddress;

	h->headerChecksum = calculateIPChecksum(h);
	assert(calculateIPChecksum(h) == 0);
}

#define MAX_IP_PAYLOAD_SIZE (MAX_IP_PACKET_SIZE - sizeof(IPV4Header))

static IPV4Header *createIPV4Header(uintptr_t dataLength, IPV4Address srcAddress, IPV4Address dstAddress){
	if(dataLength > MAX_IP_PAYLOAD_SIZE)
		return NULL;
	IPV4Header *h = allocateKernelMemory(sizeof(IPV4Header) + dataLength);
	if(h == NULL){
		return NULL;
	}
	initIPV4Header(h, dataLength, srcAddress, dstAddress, IP_DATA_PROTOCOL_TEST254);
	//memset(h->payload, 0, dataLength);
	return h;
}

static uintptr_t enumNextDataLinkDevice(uintptr_t f, const char *namePattern, FileEnumeration *fe){
	return enumNextResource(f, fe, (uintptr_t)namePattern, matchWildcardName);
}

typedef struct RWIPQueue{
	Semaphore *argCount;
	Spinlock lock;
	struct RWIPRequest *argList;
	uintptr_t fileHandle;
	uintptr_t mtu;
	int terminateFlag;
}RWIPQueue;

static uintptr_t openDataLinkDevice(const FileEnumeration *fe, uintptr_t *mtu);

static RWIPQueue *createRWIPQueue(const FileEnumeration *fe, void (*taskEntry)(void*)){
	RWIPQueue *NEW(t);
	EXPECT(t != NULL);
	uintptr_t mtu;
	uintptr_t f = openDataLinkDevice(fe, &mtu);
	EXPECT(f != IO_REQUEST_FAILURE);
	t->argCount = createSemaphore(0);
	EXPECT(t->argCount != NULL);
	t->lock = initialSpinlock;
	t->argList = NULL;
	t->fileHandle = f;
	t->mtu = mtu;
	t->terminateFlag = 0;

	Task *task = createSharedMemoryTask(taskEntry, &t, sizeof(t), processorLocalTask());
	EXPECT(task != NULL);
	resume(task);
	return t;
	// terminate task
	ON_ERROR;
	deleteSemaphore(t->argCount);
	ON_ERROR;
	syncCloseFile(f);
	ON_ERROR;
	DELETE(t);
	ON_ERROR;
	return NULL;
}

static void deleteRWIPQueue(RWIPQueue *t){
	syncCloseFile(t->fileHandle);
	deleteSemaphore(t->argCount);
	DELETE(t);
}

typedef struct RWIPRequest{
	RWFileRequest *rwfr;
	struct IPSocket *ipSocket;
	uint8_t *buffer;
	uintptr_t size;

	struct RWIPRequest **prev, *next;
}RWIPRequest;

static RWIPRequest *createRWIPArgument(RWFileRequest *rwfr, IPSocket *ips, uint8_t *buffer, uintptr_t size){
	// create RWIPRequest
	RWIPRequest *NEW(arg);
	if(arg == NULL){
		return 0;
	}
	arg->rwfr = rwfr;
	arg->ipSocket = ips;
	arg->buffer = (uint8_t*)buffer;
	arg->size = size;
	arg->prev = NULL;
	arg->next = NULL;
	return arg;
}

static void addToRWIPQueue(RWIPQueue *t, RWIPRequest *arg){
	acquireLock(&t->lock);
	ADD_TO_DQUEUE(arg, &t->argList);
	releaseLock(&t->lock);
	releaseSemaphore(t->argCount);
}

int createAddRWIPArgument(RWIPQueue *q, RWFileRequest *rwfr, IPSocket *ips, uint8_t *buffer, uintptr_t size){
	RWIPRequest *arg = createRWIPArgument(rwfr, ips, buffer, size);
	if(arg == NULL){
		return 0;
	}
	// TODO: cancellable
	addToRWIPQueue(q, arg);
	return 1;
}

static void setIPTaskTerminateFlag(RWIPQueue *t){
	acquireLock(&t->lock);
	assert(t->terminateFlag == 0);
	t->terminateFlag = 1;
	releaseLock(&t->lock);
	releaseSemaphore(t->argCount);
}

// return 0 if terminated
// return 1 if targetQueue has new value
static int waitNextIPArgument(RWIPQueue *t, RWIPRequest **targetQueue){
	assert(t->argCount != NULL);
	int listLength;
	if(*targetQueue == NULL){
		listLength = acquireAllSemaphore(t->argCount);
	}
	else{
		listLength = tryAcquireAllSemaphore(t->argCount);
	}
	acquireLock(&t->lock);
	int ok = 1;
	if(t->terminateFlag){
		ok = 0;
		assert(t->argList == NULL && listLength == 1);
		listLength--;
	}
	int a;
	for(a = 0; a < listLength; a++){
		RWIPRequest *arg = t->argList;
		REMOVE_FROM_DQUEUE(arg);
		ADD_TO_DQUEUE(arg, targetQueue);
	}
	releaseLock(&t->lock);
	return ok;
}

// one transmitTask for every device
typedef struct DataLinkDevice{
	IPV4Address bindingAddress;
	IPV4Address subnetMask;
	FileEnumeration file;

	struct DataLinkDevice **prev, *next;
}DataLinkDevice;

typedef struct{
	DataLinkDevice *head;
	Spinlock lock;
}DataLinkDeviceList;
DataLinkDeviceList dataLinkDevList = {NULL, INITIAL_SPINLOCK};

static uintptr_t openDataLinkDevice(const FileEnumeration *fe, uintptr_t *mtu){
	OpenFileMode ofm = OPEN_FILE_MODE_0;
	ofm.writable = 1;
	uintptr_t f = syncOpenFileN(fe->name, fe->nameLength, ofm);
	EXPECT(f != IO_REQUEST_FAILURE);

	uintptr_t r = syncMaxWriteSizeOfFile(f, mtu);
	EXPECT(r != IO_REQUEST_FAILURE);
	return f;
	ON_ERROR;
	syncCloseFile(f);
	ON_ERROR;
	return IO_REQUEST_FAILURE;
}

static DataLinkDevice *createDataLinkDevice(
	const FileEnumeration *fe,
	IPV4Address address, IPV4Address subnetMask
){
	DataLinkDevice *NEW(d);
	EXPECT(d != NULL);

	d->file = *fe;
	d->bindingAddress = address;
	d->subnetMask = subnetMask;
	d->prev = NULL;
	d->next = NULL;
	return d;
	//DELETE(d);
	ON_ERROR;
	return NULL;
}
/*
static void deleteDataLinkDevice(DataLinkDevice *d){
	syncCloseFile(d->fileHandle);
	assert(IS_IN_DQUEUE(d) == 0);
	setIPTaskTerminateFlag(d->transmit);
	DELETE(d);
}
*/
static void addDataLinkDevice(DataLinkDeviceList *dlList, DataLinkDevice *d){
	acquireLock(&dlList->lock);
	ADD_TO_DQUEUE(d, &dlList->head);
	releaseLock(&dlList->lock);
}

// find the first data link device whose subnet mask matches address
static DataLinkDevice *searchDeviceByIP(DataLinkDeviceList *dlList, IPV4Address address){
	acquireLock(&dlList->lock);
	DataLinkDevice *d;
	for(d = dlList->head; d != NULL; d = d->next){
		if(d->bindingAddress.value == (d->subnetMask.value & address.value)){
			break;
		}
	}
	releaseLock(&dlList->lock);
	return d;
}

int setIPAddress(IPSocket *ips, uintptr_t param, uint64_t value){
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
	return 1;
}

static int setIPParameter(FileIORequest2 *fior2, OpenedFile *of, uintptr_t param, uint64_t value){
	IPSocket *ips = getFileInstance(of);
	int ok = setIPAddress(ips, param, value);
	if(ok){
		completeFileIO0(fior2);
	}
	return ok;
}

static int writeIPSocket(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){
	IPSocket *ips = getFileInstance(of);
	return createAddRWIPArgument(ips->transmit, rwfr, ips, (uint8_t*)buffer, size);
}

static int transmitIP(RWIPQueue *tran, const RWIPRequest *arg){
	IPV4Header *packet = arg->ipSocket->createPacket(arg->ipSocket, arg->buffer, arg->size);
	EXPECT(packet != NULL);
	uintptr_t writeSize = getIPPacketSize(packet);
	EXPECT(writeSize <= tran->mtu);
	uintptr_t r = syncWriteFile(tran->fileHandle, packet, &writeSize);
	EXPECT(r != IO_REQUEST_FAILURE && writeSize == getIPPacketSize(packet));
	arg->ipSocket->deletePacket(packet);
	return 1;
	// cancel write
	ON_ERROR;
	// larger than MTU
	ON_ERROR;
	arg->ipSocket->deletePacket(packet);
	ON_ERROR;
	return 0;
}

static void transmitIPTask(void *voidArg){
	RWIPQueue *tran = *(RWIPQueue**)voidArg;
	RWIPRequest *localQueue = NULL;
	while(1){
		if(waitNextIPArgument(tran, &localQueue) == 0){
			break;
		}
		RWIPRequest *arg = localQueue;
		REMOVE_FROM_DQUEUE(arg);
		int ok = transmitIP(tran, arg);
		completeRWFileIO(arg->rwfr, (ok? arg->size: 0), 0);
		DELETE(arg); // see writeIPSocket
	}
	deleteRWIPQueue(tran);
	systemCall_terminate();
}

static int readIPSocket(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t size){
	IPSocket *ips = getFileInstance(of);
	return createAddRWIPArgument(ips->receive, rwfr, ips, buffer, size);
}

static int validateIPV4Packet(const IPV4Header *packet, uintptr_t readSize/*TODO: src/dst address*/){
	if(packet->version != 4 || readSize < sizeof(*packet)){
		return 0;
	}
	uintptr_t packetSize = getIPPacketSize(packet);
	uintptr_t headerSize = getIPHeaderSize(packet);
	if(readSize < packetSize){
		// printk("IP packet size %u < actual read size %u\n", packetSize, readSize);
	}
	if(headerSize < sizeof(*packet) || headerSize > readSize){
		printk("bad IP packet size %u; header size %u; read size %u\n", packetSize, headerSize, readSize);
		return 0;
	}
	if(calculateIPChecksum(packet) != 0){
		printk("bad IP packet checksum\n");
		return 0;
	}
	//TODO: if packet->flags
	//TODO: if(packet->destination.value & thisAddress.value){
	//
	//}
	return 1;
}

static void receiveIPTask(void *voidArg){
	RWIPQueue *rece = *(RWIPQueue**)voidArg;

	RWIPRequest *localQueue = NULL;
	IPV4Header *packet = allocateKernelMemory(rece->mtu);
	while(1){
		// wait for a valid IPv4 packet
		uintptr_t readSize = rece->mtu;
		uintptr_t r = syncReadFile(rece->fileHandle, packet, &readSize);
		if(r == IO_REQUEST_FAILURE){
			printk("cannot receive IP packet");
			continue;
		}
		if(validateIPV4Packet(packet, readSize) == 0){
			continue;
		}
		// match the packet with receiver
		if(waitNextIPArgument(rece, &localQueue) == 0){
			break; // rece->terminateFlag == 1
		}
		RWIPRequest *arg;
		uintptr_t returnSize;
		const uintptr_t packetSize = getIPPacketSize(packet);
		assert(packetSize <= readSize); // Ethernet has minimum transmission unit
		for(arg = localQueue; arg != NULL; arg = arg->next){
			// truncate packet
			returnSize = arg->size;
			if(arg->ipSocket->receivePacket(arg->ipSocket, arg->buffer, &returnSize, packet, packetSize) != 0){
				break;
			}
		}
		if(arg != NULL){
			REMOVE_FROM_DQUEUE(arg);
			completeRWFileIO(arg->rwfr, returnSize, 0);
			DELETE(arg);
		}
	}
	deleteRWIPQueue(rece);
	releaseKernelMemory(packet);
	systemCall_terminate();
}

void destroyIPSocket(IPSocket *socket){
	setIPTaskTerminateFlag(socket->receive);
	setIPTaskTerminateFlag(socket->transmit);
}

static void closeIPSocket(CloseFileRequest *cfr, OpenedFile *of){
	IPSocket *ips = getFileInstance(of);
	destroyIPSocket(ips);
	completeCloseFile(cfr);
	DELETE(ips);
}

int initIPSocket(IPSocket *socket, void *inst, unsigned *src, CreatePacket *c, ReceivePacket *r, DeletePacket *d){
	socket->instance = inst;
	socket->source.bytes[0] = src[0];
	socket->source.bytes[1] = src[1];
	socket->source.bytes[2] = src[2];
	socket->source.bytes[3] = src[3];
	socket->destination = (IPV4Address)(uint32_t)0;
	socket->createPacket = c;
	socket->receivePacket = r;
	socket->deletePacket = d;
	DataLinkDevice *dld = searchDeviceByIP(&dataLinkDevList, socket->source);
	EXPECT(dld != NULL);

	socket->transmit = createRWIPQueue(&dld->file, transmitIPTask);
	EXPECT(socket->transmit != NULL);

	socket->receive = createRWIPQueue(&dld->file, receiveIPTask);
	EXPECT(socket->receive != NULL);
	return 1;
	//setIPTaskTerminateFlag(socket->receive);
	ON_ERROR;
	setIPTaskTerminateFlag(socket->transmit);
	ON_ERROR;
	ON_ERROR;
	return 0;
}

static IPV4Header *createIPV4Packet(IPSocket *ips, const uint8_t *buffer, uintptr_t dataSize){
	IPV4Header *h = createIPV4Header(dataSize, ips->source, ips->destination);
	if(h == NULL){
		return NULL;
	}
	memcpy(((uint8_t*)h) + sizeof(*h), buffer, dataSize);
	return h;
}

static int copyIPV4Data(
	IPSocket *ips,
	uint8_t *buffer, uintptr_t *bufferSize,
	const IPV4Header *packet, uintptr_t packetSize
){
	if(packet->destination.value != ips->source.value){
		printk("warning: receive wrong IP address: %x; expect %x\n", packet->destination.value, ips->source.value);
	}
	// packet header & size are checked in validateIPV4Packet
	const uintptr_t headerSize = getIPHeaderSize(packet);
	*bufferSize = MIN(*bufferSize, packetSize - headerSize); // truncate to buffer size
	memcpy(buffer, ((uint8_t*)packet) + headerSize, *bufferSize);
	return 1;
}

static void deleteIPV4Packet(IPV4Header *h){
	releaseKernelMemory(h);
}

static int openIPSocket(OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength, OpenFileMode ofm){
	// filename = source IP
	unsigned src[4] = {0, 0, 0, 0};
	int scanCount = snscanf(fileName, nameLength, "%u.%u.%u.%u", src + 0, src + 1, src + 2, src + 3);
	EXPECT(scanCount == 4 && src[0] <= 0xff && src[1] <= 0xff && src[2] <= 0xff && src[3] <= 0xff && ofm.writable);
	// create IP socket
	IPSocket *NEW(socket);
	EXPECT(socket != NULL);
	int ok = initIPSocket(socket, socket, src, createIPV4Packet, copyIPV4Data, deleteIPV4Packet);
	EXPECT(ok);

	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.setParameter = setIPParameter;
	ff.read = readIPSocket;
	ff.write = writeIPSocket;
	ff.close = closeIPSocket;
	completeOpenFile(ofr, socket, &ff);
	return 1;
	// destroyIPSocket
	ON_ERROR;
	DELETE(socket);
	ON_ERROR;
	ON_ERROR;
	return 0;
}

void internetService(void){
	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openIPSocket;
	addFileSystem(&fnf, "ip", strlen("ip"));
	initUDP();
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
		DataLinkDevice *d = createDataLinkDevice(&fe, defaultAddress, defaultSubnetMask);
		if(d == NULL){
			printk("cannot create IP service for device\n");
		}
		addDataLinkDevice(&dataLinkDevList, d);
	}
	syncCloseFile(enumDataLink);
	systemCall_terminate();
}


#ifndef NDEBUG

static void testRWNetwork(const char *fileName, uint64_t src, uint64_t dst, int cnt, int isWrite){
	uintptr_t f = syncOpenFileN(fileName, strlen(fileName), OPEN_FILE_MODE_WRITABLE);
	assert(f != IO_REQUEST_FAILURE);
	//IPV4Address src1 = {bytes: {0, 0, 0, 1}};
	uintptr_t r;
	int i;
	for(i = 0; i < cnt; i++){
		uint8_t buffer[20];
		memset(buffer, i + 10, sizeof(buffer));
		uintptr_t rwSize = sizeof(buffer);

		r = syncSetFileParameter(f, FILE_PARAM_SOURCE_ADDRESS, src);
		assert(r != IO_REQUEST_FAILURE);
		r = syncSetFileParameter(f, FILE_PARAM_DESTINATION_ADDRESS, dst);
		assert(r != IO_REQUEST_FAILURE);
		if(isWrite){
			r = syncWriteFile(f, buffer, &rwSize);
			assert(rwSize == sizeof(buffer));
			printk("transmit packet of size %u\n", rwSize);
		}
		else{
			r = syncReadFile(f, buffer, &rwSize);
			assert(rwSize == 0 || buffer[0] != i + 10);
			printk("receive packet of size %u\n", rwSize);
			unsigned j;
			for(j = 0; j < rwSize && j < 15; j++){
				printk("%u(%c) ", buffer[j], buffer[j]);
			}
			printk("\n");
		}
		assert(r != IO_REQUEST_FAILURE);
	}
	r = syncCloseFile(f);
	assert(r != IO_REQUEST_FAILURE);
	printk("test %s %s ok\n", (isWrite? "write": "read"), fileName);
}

static void testRWIP(int cnt, int isWrite){
	int ok = waitForFirstResource("ip", RESOURCE_FILE_SYSTEM, matchName);
	assert(ok);
	IPV4Address src0 = {bytes: {0, 0, 0, 0}};
	uint16_t srcPort = 60000;
	IPV4Address dst0 = {bytes: {0, 0, 0, 0}};
	uint16_t dstPort = 0;
	// wait for device
	sleep(1000);
	testRWNetwork("ip:0.0.0.0", src0.value, dst0.value, cnt, isWrite);
	testRWNetwork("udp:0.0.0.0:60000",
		src0.value + (((uint64_t)srcPort) << 32),
		dst0.value + (((uint64_t)dstPort) << 32), cnt, isWrite);
	systemCall_terminate();
}

void testWriteIP(void);
void testWriteIP(void){
	testRWIP(30, 1);
}
/*
static void testReadNetwork(const char *fileName){
	uintptr_t f = syncOpenFileN(fileName, strlen(fileName), OPEN_FILE_MODE_WRITABLE);
	assert(f != IO_REQUEST_FAILURE);
	uintptr_t r;
	int i;
	for(i = 0 ; i < 5; i++){
		uint8_t buffer[80];
		uintptr_t readSize = sizeof(buffer);
		memset(buffer, 0, readSize);
		r = syncReadFile(f ,buffer, &readSize);
		assert(r != IO_REQUEST_FAILURE);
		printk("receive packet of size %u\n", readSize);
		unsigned j;
		for(j = 0; j < readSize && j < 10; j++){
			printk("%x ", buffer[j]);
		}
		printk("\n");
	}
	r = syncCloseFile(f);
	assert(r != IO_REQUEST_FAILURE);
	printk("test read ip ok\n");
	systemCall_terminate();
}
*/
void testReadIP(void);
void testReadIP(void){
	testRWIP(3, 0);
}

#endif
