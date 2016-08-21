#include"memory/memory.h"
#include"resource/resource.h"
#include"task/task.h"
#include"task/exclusivelock.h"
#include"multiprocessor/processorlocal.h"
#include"io/fifo.h"
#include"network.h"
#include"kernel.h"

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

void *getIPData(const IPV4Header *h){
	return ((uint8_t*)h) + getIPHeaderSize(h);
}

// return big endian number
static uint16_t calculateIPHeaderChecksum(const IPV4Header *h){
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

// return little endian number which can be greater than 0xffff
static uint32_t calculatePseudoIPHeaderChecksum(IPV4Address src, IPV4Address dst, uint8_t protocol, uintptr_t dataSize){
	uint32_t cs = 0;
	cs += changeEndian16(src.value & 0xffff);
	cs += changeEndian16(src.value >> 16);
	cs += changeEndian16(dst.value & 0xffff);
	cs += changeEndian16(dst.value >> 16);
	cs += protocol;
	cs += dataSize;
	return cs;
}

uint16_t calculateIPDataChecksum2(
	const void *voidIPData, uintptr_t dataSize,
	IPV4Address src, IPV4Address dst, uint8_t protocol
){
	// pseudo ip header
	uint32_t cs = calculatePseudoIPHeaderChecksum(src, dst, protocol, dataSize);
	// udp header + udp data
	uintptr_t i;
	for(i = 0; (i + 1) * sizeof(uint16_t) <= dataSize; i++){
		cs += (uint32_t)changeEndian16(((const uint16_t*)voidIPData)[i]);
	}
	// padding
	if(dataSize % sizeof(uint16_t) != 0){
		uint16_t lastByte = (uint16_t)(((const uint8_t*)voidIPData)[dataSize - 1]);
		cs += (uint32_t)changeEndian16(lastByte);
	}
	while(cs > 0xffff){
		cs = (cs & 0xffff) + (cs >> 16);
	}
	cs = changeEndian16(cs ^ 0xffff);
	return cs;
}

uint16_t calculateIPDataChecksum(const IPV4Header *h){
	return calculateIPDataChecksum2(getIPData(h), getIPDataSize(h), h->source, h->destination, h->protocol);
}

void initIPV4Header(
	IPV4Header *h, uint16_t dataSize, IPV4Address localAddr, IPV4Address remoteAddr,
	enum IPDataProtocol dataProtocol
){
	h->version = 4;
	// no optional entries
	h->headerLength = sizeof(IPV4Header) / sizeof(uint32_t);
	h->differentiateService = 0;
	h->congestionNotification = 0;
	h->totalLength = changeEndian16(sizeof(IPV4Header) + dataSize);
	h->identification = changeEndian16(0);
	h->fragmentOffsetHigh = 0;
	h->flags = 2; // 2 = don't fragment; 4 = more fragment
	h->fragmentOffsetLow = 0;
	h->timeToLive = 32;
	h->protocol = dataProtocol;
	h->headerChecksum = 0;
	h->source = localAddr;
	h->destination = remoteAddr;

	h->headerChecksum = calculateIPHeaderChecksum(h);
	assert(calculateIPHeaderChecksum(h) == 0);
}

#define MAX_IP_PAYLOAD_SIZE (MAX_IP_PACKET_SIZE - sizeof(IPV4Header))

static IPV4Header *createIPV4Header(
	uintptr_t dataSize, IPV4Address localAddr, IPV4Address remoteAddr,
	enum IPDataProtocol protocol
){
	if(dataSize > MAX_IP_PAYLOAD_SIZE)
		return NULL;
	IPV4Header *h = allocateKernelMemory(sizeof(IPV4Header) + dataSize);
	if(h == NULL){
		return NULL;
	}
	initIPV4Header(h, dataSize, localAddr, remoteAddr, protocol);
	return h;
}

static int isBroadcastIPV4Address(IPV4Address address, IPV4Address subnet, IPV4Address mask){
	if(address.value == 0xffffffff){ // 255.255.255.255 is IP local broadcast address
		return 1;
	}
	if((address.value & mask.value) == (subnet.value & mask.value) && (address.value & ~mask.value) == ~mask.value){
		return 1;
	}
	return 0;
}

static uintptr_t enumNextDataLinkDevice(uintptr_t f, const char *namePattern, FileEnumeration *fe){
	return enumNextResource(f, fe, (uintptr_t)namePattern, matchWildcardName);
}

typedef struct RWIPQueue{
	Semaphore *argCount;
	Spinlock lock;
	struct RWIPRequest *argList;
	int terminateFlag;
	IPSocket *socket;
}RWIPQueue;

static void addIPSocketReference(IPSocket *ips, int v);

// the device file is opened by the internetService() thread,
// so it is not writable by the socket thread
// the sharedMemoryTask should be
static RWIPQueue *createRWIPQueue(void (*taskEntry)(void*), IPSocket *ipSocket, Task *sharedMemoryTask){
	RWIPQueue *NEW(t);
	EXPECT(t != NULL);
	t->argCount = createSemaphore(0);
	EXPECT(t->argCount != NULL);
	t->lock = initialSpinlock;
	t->argList = NULL;
	t->terminateFlag = 0;
	t->socket = ipSocket;
	Task *task = createSharedMemoryTask(taskEntry, &t, sizeof(t), sharedMemoryTask);
	EXPECT(task != NULL);
	addIPSocketReference(t->socket, 1);
	resume(task);
	return t;
	// terminate task
	ON_ERROR;
	deleteSemaphore(t->argCount);
	ON_ERROR;
	DELETE(t);
	ON_ERROR;
	return NULL;
}

static void deleteRWIPQueue(RWIPQueue *t){
	addIPSocketReference(t->socket, -1);
	deleteSemaphore(t->argCount);
	DELETE(t);
}

typedef struct RWIPRequest{
	RWFileRequest *rwfr;
	struct IPSocket *ipSocket;
	uint8_t *buffer;
	uintptr_t size;

	RWIPQueue *queue;
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
	arg->queue = NULL;
	arg->prev = NULL;
	arg->next = NULL;
	return arg;
}

static void cancelRWIPRequest(void *voidArg){
	RWIPRequest *arg = voidArg;
	int ok = tryAcquireSemaphore(arg->queue->argCount);
	if(!ok){
		assert(0);
	}
	acquireLock(&arg->queue->lock);
	REMOVE_FROM_DQUEUE(arg);
	releaseLock(&arg->queue->lock);
	DELETE(arg);
}

static void addToRWIPQueue(RWIPQueue *q, RWIPRequest *arg){
	assert(arg->queue == NULL);
	arg->queue = q;
	acquireLock(&q->lock);
	ADD_TO_DQUEUE(arg, &q->argList);
	setRWFileIOCancellable(arg->rwfr, arg, cancelRWIPRequest);
	releaseLock(&q->lock);
	releaseSemaphore(q->argCount);
}

int createAddRWIPArgument(RWIPQueue *q, RWFileRequest *rwfr, IPSocket *ips, uint8_t *buffer, uintptr_t size){
	assert(q != NULL); // call startIPSocket
	RWIPRequest *arg = createRWIPArgument(rwfr, ips, buffer, size);
	if(arg == NULL){
		return 0;
	}
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

// if success, return 1
// if¡@the socket is closed, return 0
int nextRWIPRequest(RWIPQueue *q, RWFileRequest **rwfr, uint8_t **buffer, uintptr_t *size){
	assert(q->argCount != NULL);
	acquireSemaphore(q->argCount);
	acquireLock(&q->lock);
	const int t = q->terminateFlag;
	RWIPRequest *r = q->argList;
	if(t == 0){
		assert(r != NULL && r->queue == q);
			REMOVE_FROM_DQUEUE(r);
			setRWFileIONotCancellable(r->rwfr);
			r->queue = NULL;
	}
	releaseLock(&q->lock);
	assert(r != NULL || t != 0);
	if(r != NULL){
		*rwfr = r->rwfr;
		*buffer = r->buffer;
		*size = r->size;
		DELETE(r);
	}
	return (r != NULL);
}

/*
enum DeviceIPAddressType{
	DEVICE_IP_ADDR_DHCP,
	DEVICE_IP_ADDR_STATIC
};
*/
typedef struct DataLinkDevice{
	IPConfig ipConfig;
	Spinlock ipConfigLock;

	uintptr_t mtu;
	FileEnumeration fileEnumeration;
	uintptr_t fileHandle;

	DHCPClient *dhcpClient;
	ARPServer *arpServer;

	struct DataLinkDevice **prev, *next;
}DataLinkDevice;

typedef struct{
	DataLinkDevice *head;
	Spinlock lock;
}DataLinkDeviceList;
static DataLinkDeviceList dataLinkDevList = {NULL, INITIAL_SPINLOCK};
// see initInternetService

static void ipDeviceReader(void *voidArg);

static DataLinkDevice *createDataLinkDevice(const FileEnumeration *fe){
	DataLinkDevice *NEW(d);
	EXPECT(d != NULL);

	d->fileEnumeration = *fe;
	d->fileHandle = syncOpenFileN(fe->name, fe->nameLength, OPEN_FILE_MODE_WRITABLE);
	EXPECT(d->fileHandle != IO_REQUEST_FAILURE);
	uintptr_t r = syncMaxWriteSizeOfFile(d->fileHandle, &d->mtu);
	EXPECT(r != IO_REQUEST_FAILURE);
	d->ipConfigLock = initialSpinlock;
	d->ipConfig.localAddress = ANY_IPV4_ADDRESS;
	d->ipConfig.subnetMask = ANY_IPV4_ADDRESS; // broadcast address = 255.255.255.255
	d->ipConfig.dhcpServer = ANY_IPV4_ADDRESS;
	d->ipConfig.gateway = ANY_IPV4_ADDRESS;
	d->ipConfig.dnsServer = ANY_IPV4_ADDRESS;
	uint64_t macAddress;
	r = syncGetFileParameter(d->fileHandle, FILE_PARAM_SOURCE_ADDRESS, &macAddress);
	EXPECT(r != IO_REQUEST_FAILURE);
	d->dhcpClient = createDHCPClient(fe, &d->ipConfig, &d->ipConfigLock, macAddress);
	EXPECT(d->dhcpClient != NULL);
	d->arpServer = createARPServer(fe, &d->ipConfig, &d->ipConfigLock, macAddress);
	EXPECT(d->arpServer != NULL);
	d->prev = NULL;
	d->next = NULL;
	Task *t = createSharedMemoryTask(ipDeviceReader, &d, sizeof(d), processorLocalTask());
	EXPECT(t != NULL);
	resume(t);
	return d;
	// terminate task
	ON_ERROR;
	// TODO: terminate ARP server
	ON_ERROR;
	// TODO: delete DHCP client
	ON_ERROR;
	// mac address
	ON_ERROR;
	// mtu
	ON_ERROR;
	syncCloseFile(d->fileHandle);
	ON_ERROR;
	DELETE(d);
	ON_ERROR;
	return NULL;
}
/*
static void deleteDataLinkDevice(DataLinkDevice *d){

	DELETE(d);
}
*/
static void addDataLinkDevice(DataLinkDeviceList *dlList, DataLinkDevice *d){
	acquireLock(&dlList->lock);
	ADD_TO_DQUEUE(d, &dlList->head);
	releaseLock(&dlList->lock);
}

static DataLinkDevice *searchDeviceByName(DataLinkDeviceList *devList, const char *name, uintptr_t nameLength){
	acquireLock(&devList->lock);
	DataLinkDevice *d;
	for(d= devList->head; d != NULL; d = d->next){
		if(isStringEqual(d->fileEnumeration.name, d->fileEnumeration.nameLength, name, nameLength)){
			break;
		}
	}
	releaseLock(&devList->lock);
	return d;
}

static DataLinkDevice *searchDeviceByRoutingTable(DataLinkDeviceList *devList, __attribute__((__unused__)) IPV4Address address){
	acquireLock(&devList->lock);
	DataLinkDevice *d;
	// TODO:
	for(d = devList->head; d != NULL; d = d->next){
		int ok = 1;
		// (d->ipConfig.bindingAddress.value & d->ipConfig.subnetMask.value) ==
		// (address.value & d->ipConfig.subnetMask.value);
		if(ok){
			break;
		}
	}
	releaseLock(&devList->lock);
	return d;
}

int getIPSocketParam(IPSocket *ips, uintptr_t param, uint64_t *value){
	*value = 0;
	switch(param){
	case FILE_PARAM_MAX_WRITE_SIZE:
		{
			IPV4Address a;
			DataLinkDevice *d = resolveLocalAddress(ips, &a);
			if(d == NULL){
				return 0;
			}
			*value = d->mtu - sizeof(IPV4Header);
		}
		break;
	case FILE_PARAM_SOURCE_ADDRESS:
		{
			IPV4Address a;
			if(resolveLocalAddress(ips, &a) != NULL){
				*value = a.value;
				break;
			}
			*value = ips->localAddress.value; // IMPROVE: atomic
		}
		break;
	case FILE_PARAM_DESTINATION_ADDRESS:
		*value = ips->remoteAddress.value;
		break;
	case FILE_PARAM_SOURCE_PORT:
		*value = ips->localPort;
		break;
	case FILE_PARAM_DESTINATION_PORT:
		*value = ips->remotePort;
		break;
	}
	return 1;
}

int setIPSocketParam(IPSocket *ips, uintptr_t param, uint64_t value){
	switch(param){
	case FILE_PARAM_SOURCE_ADDRESS:
		ips->localAddress = (IPV4Address)(uint32_t)value; // IMPROVE: atomic
		break;
	case FILE_PARAM_DESTINATION_ADDRESS:
		ips->remoteAddress = (IPV4Address)(uint32_t)value;
		break;
	case FILE_PARAM_SOURCE_PORT:
		ips->localPort = (uint16_t)value;
		break;
	case FILE_PARAM_DESTINATION_PORT:
		ips->remotePort = (uint16_t)value;
		break;
	default:
		return 0;
	}
	return 1;
}

static int getIPParameter(FileIORequest2 *fior2, OpenedFile *of, uintptr_t param){
	IPSocket *ips = getFileInstance(of);
	uint64_t value;
	int ok = getIPSocketParam(ips, param, &value);
	if(ok){
		completeFileIO64(fior2, value);
	}
	return ok;
}

static int setIPParameter(FileIORequest2 *fior2, OpenedFile *of, uintptr_t param, uint64_t value){
	IPSocket *ips = getFileInstance(of);
	int ok = setIPSocketParam(ips, param, value);
	if(ok){
		completeFileIO0(fior2);
	}
	return ok;
}

static int writeIPSocket(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){
	IPSocket *ips = getFileInstance(of);
	return createAddRWIPArgument(ips->transmit, rwfr, ips, (uint8_t*)buffer, size);
}

DataLinkDevice *resolveLocalAddress(const IPSocket *s, IPV4Address *a){
	DataLinkDeviceList *devList = &dataLinkDevList;
	// device
	DataLinkDevice *d = NULL;
	if(s->bindToDevice){
		d = searchDeviceByName(devList, s->deviceName, s->deviceNameLength);
	}
	else{
		d = searchDeviceByRoutingTable(devList, s->remoteAddress);
	}
	if(d == NULL)
		return NULL;
	// local address
	if(s->localAddress.value != ANY_IPV4_ADDRESS.value){
		*a = s->localAddress;
	}
	else{
		acquireLock(&d->ipConfigLock);
		*a = d->ipConfig.localAddress;
		releaseLock(&d->ipConfigLock);
	}
	return d;
}

int transmitIPPacket(DataLinkDevice *device, const IPV4Header *packet){
	uintptr_t packetSize = getIPPacketSize(packet);
	uintptr_t writeSize = packetSize;
	if(writeSize > device->mtu){
		return 0;
	}
	uintptr_t r = syncWriteFile(device->fileHandle, packet, &writeSize);
	if(r == IO_REQUEST_FAILURE){
		return 0;
	}
	return (writeSize == packetSize);
}

static void transmitIPTask(void *voidArg){
	RWIPQueue *tran = *(RWIPQueue**)voidArg;
	IPSocket *ips = tran->socket;
	while(ips->transmitPacket(ips) != 0){
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
		printk("IP packet version = %u; read size %u\n", packet->version, readSize);
		return 0;
	}
	uintptr_t packetSize = getIPPacketSize(packet);
	uintptr_t headerSize = getIPHeaderSize(packet);
	if(packetSize > readSize){
		printk("%x\n",packet->flags);
		printk("IP packet size %u > actual read size %u\n", packetSize, readSize);
		return 0;
	}
	if(headerSize < sizeof(*packet) || headerSize > readSize){
		printk("bad IP packet size %u; header size %u; read size %u\n", packetSize, headerSize, readSize);
		return 0;
	}
	if(calculateIPHeaderChecksum(packet) != 0){
		printk("bad IP packet checksum\n");
		return 0;
	}
	//TODO: if packet->flags
	//TODO: if(packet->destination.value & thisAddress.value){
	//
	//}
	return 1;
}

typedef struct IPFIFO{
	FIFO *fifo;
	struct IPFIFO **prev, *next;
}IPFIFO;

struct IPFIFOList{
	Semaphore *semaphore;
	IPFIFO *head;
};

static int initIPFIFOList(struct IPFIFOList *ifl){
	ifl->semaphore = createSemaphore(1);
	if(ifl->semaphore == NULL){
		return 0;
	}
	ifl->head = NULL;

	return 1;
}

static void addToIPFIFOList(struct IPFIFOList *ifl, IPFIFO *ipf){
	acquireSemaphore(ifl->semaphore);
	ADD_TO_DQUEUE(ipf, &ifl->head);
	releaseSemaphore(ifl->semaphore);
}

static void removeFromIPFIFOList(struct IPFIFOList *ifl, IPFIFO *ipf){
	acquireSemaphore(ifl->semaphore);
	REMOVE_FROM_DQUEUE(ipf);
	releaseSemaphore(ifl->semaphore);
}

struct QueuedPacket{
	DataLinkDevice *fromDevice;
	int isBoradcast;
	ReferenceCount referenceCount;
	IPV4Header packet[];
};

static IPFIFO *createIPFIFO(uintptr_t maxLength){
	IPFIFO *NEW(ipf);
	EXPECT(ipf != NULL);
	ipf->fifo = createFIFO(maxLength, sizeof(QueuedPacket*));
	EXPECT(ipf->fifo != NULL);
	ipf->prev = NULL;
	ipf->next = NULL;
	return ipf;
	ON_ERROR;
	ON_ERROR;
	return NULL;
}

static QueuedPacket *createQueuedPacket(IPV4Header *packet, DataLinkDevice *device){
	const uintptr_t packetSize = getIPPacketSize(packet);
	QueuedPacket *p = allocateKernelMemory(sizeof(QueuedPacket) + packetSize);
	if(p == NULL){
		return NULL;
	}
	p->fromDevice = device; // IMPROVE: add reference count if we want to delete device
	acquireLock(&device->ipConfigLock);
	IPV4Address devAddress = device->ipConfig.localAddress;
	IPV4Address devMask = device->ipConfig.subnetMask;
	releaseLock(&device->ipConfigLock);
	p->isBoradcast = isBroadcastIPV4Address(packet->destination, devAddress, devMask);
	initReferenceCount(&p->referenceCount, 0);
	memcpy(p->packet, packet, packetSize);
	return p;
}

const IPV4Header *getQueuedPacketHeader(QueuedPacket *p){
	return p->packet;
}

void addQueuedPacketRef(QueuedPacket *p, int n){
	if(addReference(&p->referenceCount, n) == 0){
		releaseKernelMemory(p);
	}
}

static void overwriteIPFIFO(IPFIFO *f, QueuedPacket *p){
	QueuedPacket *dropped = NULL;
	addQueuedPacketRef(p, 1);
	if(overwriteFIFO(f->fifo, &p, &dropped) == 0){
		addQueuedPacketRef(dropped, -1);
	}
}

static QueuedPacket *readIPFIFO(IPFIFO *f){
	QueuedPacket *r;
	readFIFO(f->fifo, &r);
	return r;
}

static void deleteIPFIFO(IPFIFO *ipf){
	assert(IS_IN_DQUEUE(ipf) == 0);
	QueuedPacket *p;
	while(readFIFONonBlock(ipf->fifo, &p) != 0){
		addQueuedPacketRef(p, -1);
	}
	deleteFIFO(ipf->fifo);
	DELETE(ipf);
}

struct IPService{
	struct IPFIFOList readFIFOList;
	Task *mainTask;
};

static struct IPService ipService;

static int openIPSocket(OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength, OpenFileMode ofm);

static void initIP(void){
	if(initIPFIFOList(&ipService.readFIFOList) == 0){
		panic("cannot initialize IP FIFO");
	}
	ipService.mainTask = processorLocalTask();

	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openIPSocket;
	if(addFileSystem(&fnf, "ip", strlen("ip")) == 0){
		panic("cannot create IP service");
	}
}

static void ipDeviceReader(void *voidArg){
	DataLinkDevice *dev = *(DataLinkDevice**)voidArg;
	const uintptr_t fileHandle = dev->fileHandle;
	const uintptr_t mtu = dev->mtu;
	const struct IPFIFOList *const ipFIFOList = &ipService.readFIFOList;
	uintptr_t r;
	IPV4Header *const packet = allocateKernelMemory(mtu);
	if(packet == NULL){
		systemCall_terminate();
	}
	while(1){
		uintptr_t readSize = mtu;
		r = syncReadFile(fileHandle, packet, &readSize);
		if(r == IO_REQUEST_FAILURE){
			break;
		}
		if(validateIPV4Packet(packet, readSize) == 0){
			continue;
		}
		QueuedPacket *qp = createQueuedPacket(packet, dev);
		if(qp == NULL){
			printk("warning: insufficient memory for IP buffer\n");
			continue;
		}
		addQueuedPacketRef(qp, 1);
		acquireSemaphore(ipFIFOList->semaphore);
		IPFIFO *ipf;
		for(ipf = ipFIFOList->head; ipf != NULL; ipf = ipf->next){
			// IMPROVE: check socket's IP address, device name... here
			overwriteIPFIFO(ipf, qp);
		}
		releaseSemaphore(ipFIFOList->semaphore);
		addQueuedPacketRef(qp, -1);
	}
	releaseKernelMemory(packet);
	systemCall_terminate();
}

static int filterIPV4PacketDevice(const IPSocket *s, const DataLinkDevice *d){
	if(s->bindToDevice == 0){
		return 1;
	}
	return isStringEqual(s->deviceName, s->deviceNameLength, d->fileEnumeration.name, d->fileEnumeration.nameLength);
}

static int filterIPV4PacketAddress(const IPSocket *ips, const IPV4Header *packet, int isBroadcast){
	// local address
	if(
		isBroadcast == 0 &&
		ips->localAddress.value != packet->destination.value &&
		ips->localAddress.value != ANY_IPV4_ADDRESS.value
	){
		//printk("warning: receive wrong IP address: %x; expect %x\n", packet->destination.value, ips->localAddress.value);
		return 0;
	}
	// remote address
	if(ips->remoteAddress.value != packet->source.value && ips->remoteAddress.value != ANY_IPV4_ADDRESS.value){
		return 0;
	}
	return 1;
}

static int filterQueuedPacket(IPSocket *s, QueuedPacket *qp){
	if(filterIPV4PacketDevice(s, qp->fromDevice) == 0){
		return 0;
	}
	if(filterIPV4PacketAddress(s, qp->packet, qp->isBoradcast) == 0){
		return 0;
	}
	if(s->filterPacket(s, qp->packet, getIPPacketSize(qp->packet)) == 0){
		return 0;
	}
	return 1;
}

static void flushRWIPRequests(RWIPQueue *q){
	while(1){
		RWFileRequest *rwfr;
		uint8_t *buffer;
		uintptr_t size;
		if(nextRWIPRequest(q, &rwfr, &buffer, &size) == 0){
			break;
		}
		completeRWFileIO(rwfr, 0, 0);
	}
}

static void receiveIPTask(void *voidArg){
	RWIPQueue *rece = *(RWIPQueue**)voidArg;
	IPSocket *ips = rece->socket;
	IPFIFO *const ipFIFO = createIPFIFO(64);
	struct IPFIFOList *const ipFIFOList = &ipService.readFIFOList;
	EXPECT(ipFIFO != NULL);
	addToIPFIFOList(ipFIFOList, ipFIFO);
	while(1){
		// wait for a valid IPv4 packet
		QueuedPacket *qp = readIPFIFO(ipFIFO);
		int continueFlag = 1;
		if(filterQueuedPacket(ips, qp)){
			continueFlag = ips->receivePacket(ips, qp);
		}
		addQueuedPacketRef(qp, -1);
		if(continueFlag == 0){
			break;
		}
	}
	removeFromIPFIFOList(ipFIFOList, ipFIFO);
	deleteIPFIFO(ipFIFO);
	deleteRWIPQueue(rece);
	systemCall_terminate();
	// no return
	// deleteIPFIFO(ipFIFO);
	ON_ERROR;
	flushRWIPRequests(rece);
	deleteRWIPQueue(rece);
	systemCall_terminate();
}

void initIPSocket(
	IPSocket *s, void *inst, enum IPDataProtocol p,
	TransmitPacket *t, FilterPacket *f, ReceivePacket *r, DeleteSocket *d
){
	s->instance = inst;
	s->protocol = p;
	s->localAddress = ANY_IPV4_ADDRESS;
	s->localPort = 0;
	s->remoteAddress = ANY_IPV4_ADDRESS;
	s->remotePort = 0;
	s->bindToDevice = 0;
	memset(s->deviceName, 0, sizeof(s->deviceName));
	s->deviceNameLength = 0;
	s->transmitPacket = t;
	s->filterPacket = f;
	s->receivePacket = r;
	s->deleteSocket = d;
	initReferenceCount(&s->referenceCount, 1);
	s->receive = NULL;
	s->transmit = NULL;
}

static IPSocket *createIPSocket(
	void *inst,
	TransmitPacket *t, FilterPacket *f, ReceivePacket *r, DeleteSocket *d
){
	IPSocket *NEW(s);
	if(s == NULL){
		return NULL;
	}
	initIPSocket(s, inst, IP_DATA_PROTOCOL_TEST254, t, f, r, d);
	return s;
}

static void deleteIPSocket(IPSocket *s){
	DELETE(s);
}

static void addIPSocketReference(IPSocket *ips, int v){
	if(addReference(&ips->referenceCount, v) == 0){
		ips->deleteSocket(ips);
	}
}

// TODO: see tcp.c
Task *getIPServiceTask(void);
Task *getIPServiceTask(void){
	return ipService.mainTask;
}

int startIPSocketTasks(IPSocket *socket){
	assert(socket->transmit == NULL && socket->receive == NULL);
	socket->receive = createRWIPQueue(receiveIPTask, socket, ipService.mainTask);
	EXPECT(socket->receive != NULL);
	socket->transmit = createRWIPQueue(transmitIPTask, socket, ipService.mainTask);
	EXPECT(socket->transmit != NULL);
	return 1;
	//setIPTaskTerminateFlag(socket->transmit);
	//socket->transmit = NULL;
	ON_ERROR;
	setIPTaskTerminateFlag(socket->receive);
	socket->receive = NULL;
	ON_ERROR;
	return 0;
}

void stopIPSocketTasks(IPSocket *socket){
	if(socket->receive != NULL)
		setIPTaskTerminateFlag(socket->receive);
	if(socket->transmit != NULL)
		setIPTaskTerminateFlag(socket->transmit);
	addIPSocketReference(socket, -1);
}

static void closeIPSocket(CloseFileRequest *cfr, OpenedFile *of){
	IPSocket *ips = getFileInstance(of);
	stopIPSocketTasks(ips);
	completeCloseFile(cfr);
}

static IPV4Header *createIPV4Packet(
	IPSocket *s, IPV4Address src, IPV4Address dst,
	const uint8_t *buffer, uintptr_t dataSize
){
	IPV4Header *h = createIPV4Header(dataSize, src, dst, s->protocol);
	if(h == NULL){
		return NULL;
	}
	memcpy(((uint8_t*)h) + sizeof(*h), buffer, dataSize);
	return h;
}

static void deleteIPV4Packet(IPV4Header *h){
	releaseKernelMemory(h);
}

int transmitSinglePacket(IPSocket *s, CreatePacket *createPacket, DeletePacket *deletePacket){
	RWIPQueue *tran = s->transmit;
	RWFileRequest *rwfr;
	uint8_t *buffer;
	uintptr_t size;
	int ok = nextRWIPRequest(tran, &rwfr, &buffer, &size);
	if(!ok){
		return 0;
	}

	IPV4Address src, dst = s->remoteAddress;
	DataLinkDevice *dld = resolveLocalAddress(s, &src);
	EXPECT(dld != NULL);
	IPV4Header *packet = createPacket(s, src, dst, buffer, size);
	EXPECT(packet != NULL);
	ok = transmitIPPacket(dld, packet);
	EXPECT(ok);
	deletePacket(packet);
	completeRWFileIO(rwfr, size, 0);
	return 1;

	// cancel write
	ON_ERROR;
	deletePacket(packet);
	ON_ERROR;
	// no device
	ON_ERROR;
	completeRWFileIO(rwfr, 0, 0);
	return 0;
}

int transmitIPV4Packet(IPSocket *ips){
	return transmitSinglePacket(ips, createIPV4Packet, deleteIPV4Packet);
}

static int filterIPV4Packet(
	__attribute__((__unused__)) IPSocket *ipSocket,
	__attribute__((__unused__)) const IPV4Header *packet,
	__attribute__((__unused__)) uintptr_t packetSize
){
	//see filterQueuedPacket
	return 1;
}

static uintptr_t copyIPV4PacketData(uint8_t *buffer, uintptr_t bufferSize,	const IPV4Header *packet
){
	uintptr_t returnSize = getIPDataSize(packet);
	returnSize = MIN(bufferSize, returnSize); // truncate
	memcpy(buffer, getIPData(packet), returnSize);
	return returnSize;
}

int receiveSinglePacket(IPSocket *ips, QueuedPacket *qp, CopyPacketData *copyPacketData){
	RWIPQueue *rece = ips->receive;
	// match the packet with receiver
	RWFileRequest *rwfr;
	uint8_t *buffer;
	uintptr_t size;
	int ok = nextRWIPRequest(rece, &rwfr, &buffer, &size);
	if(!ok){
		return 0;
	}
	const IPV4Header *packet = getQueuedPacketHeader(qp);
	uintptr_t returnSize = copyPacketData(buffer, size, packet);
	completeRWFileIO(rwfr, returnSize, 0);
	return 1;
}

int receiveIPV4Packet(IPSocket *ips, QueuedPacket *qp){
	return receiveSinglePacket(ips, qp, copyIPV4PacketData);
}

static int scanPort(const char *name, uintptr_t nameLength, uint16_t *port, unsigned *scanLength){
	unsigned p;
	int scanCount = snscanf(name, nameLength, "%u%n", &p, scanLength);
	if(scanCount == 0 || p > 0xffff){
		return 0;
	}
	*port = (uint16_t)p;
	return 1;
}

static int scanSubnet(const char *name, uintptr_t nameLength, IPV4Address *subnet, unsigned *scanLength){
	unsigned s;
	int scanCount = snscanf(name, nameLength, "%u%n", &s, scanLength);
	if(scanCount == 0 || s > 32){
		return 0;
	}
	if(s == 0){ // avoid 1 << 32
		subnet->value = 0;
	}
	else{
		subnet->value = changeEndian32(~((((uint32_t)1) << (32 - s)) - 1));
	}
	return 1;
}

// if subnet or port is NULL, it is not parsed
static int scanIPAddress(
	const char *name, uintptr_t nameLength,
	IPV4Address *address, IPV4Address *subnet, uint16_t *port, unsigned *scanLength
){
	unsigned scanIndex = 0;
	int scanCount = 0;
	// ip address
	if(snscanf(name, nameLength, "%I%n", &address->value, &scanIndex) != 1){
		return 0;
	}
	scanCount++;
	// subnet
	if(subnet != NULL && scanIndex < nameLength && name[scanIndex] == '/'){
		scanIndex++;
		unsigned scanLength2;
		int scanCount2 = scanSubnet(name + scanIndex, nameLength - scanIndex, subnet, &scanLength2);
		if(scanCount2 == 0){
			return 0;
		}
		scanIndex += scanLength2;
		scanCount += scanCount2;
	}
	// port
	if(port != NULL && scanIndex < nameLength && name[scanIndex] == ':'){
		scanIndex++;
		unsigned scanLength2;
		int scanCount2 = scanPort(name + scanIndex, nameLength - scanIndex, port, &scanLength2);
		if(scanCount2 == 0){
			return 0;
		}
		scanIndex += scanLength2;
		scanCount += scanCount2;
	}

	*scanLength = scanIndex;
	return scanCount;
}

static int scanIPSocketArgument(IPSocket *s, const char *k, uintptr_t keyLen, const char *v, uintptr_t vLen){
	uintptr_t scanLength;
	int r;
	if(isStringEqual(k, keyLen, "dst", strlen("dst"))){
		r = scanIPAddress(v, vLen, &s->remoteAddress, NULL, &s->remotePort, &scanLength);
	}
	else if(isStringEqual(k, keyLen, "src", strlen("src"))){
		r = scanIPAddress(v, vLen, &s->localAddress, NULL, &s->localPort, &scanLength);
	}
	else if(isStringEqual(k, keyLen, "srcport", strlen("srcport"))){
		r = scanPort(v, vLen, &s->localPort, &scanLength);
	}
	else if(isStringEqual(k, keyLen, "dstport", strlen("dstport"))){
		r = scanPort(v, vLen, &s->remotePort, &scanLength);
	}
	else if(isStringEqual(k, keyLen, "dev", strlen("dev"))){
		if(vLen > LENGTH_OF(s->deviceName)){
			r = 0;
			scanLength = 0;
		}
		else{
			r = 1;
			scanLength = vLen;
			s->bindToDevice = 1;
			s->deviceNameLength = vLen;
			memcpy(s->deviceName, v, vLen * sizeof(v[0]));
		}
	}
	else{
		r = 0;
		scanLength = 0;
	}
	return r > 0 && scanLength == vLen;
}

// ip:REMOTE;src=LOCAL;dev=DEVICE
int scanIPSocketArguments(IPSocket *s, const char *fileName, uintptr_t nameLength){
	const char separator = ';';
	const char separator2 = '=';
	uintptr_t scanLength;
	int ok;
	// remote address
	ok = scanIPAddress(fileName, nameLength, &s->remoteAddress, NULL, &s->remotePort, &scanLength);
	if(ok == 0){
		return 0;
	}
	fileName += scanLength;
	nameLength -= scanLength;
	while(1){
		// separator
		if(nameLength == 0){
			return 1;
		}
		if(fileName[0] != separator){
			return 0;
		}
		fileName++;
		nameLength--;
		// key=value
		if(nameLength == 0){
			return 1;
		}
		uintptr_t keyEnd = indexOf(fileName, 0, nameLength, separator2);
		if(keyEnd == nameLength){
			return 0;
		}
		uintptr_t valueEnd = indexOf(fileName, keyEnd + 1, nameLength, separator);
		ok = scanIPSocketArgument(s, fileName, keyEnd, fileName + keyEnd + 1, valueEnd - (keyEnd + 1));
		if(ok == 0){
			return 0;
		}
		fileName += valueEnd;
		nameLength -= valueEnd;
	}
	return 1;
}

static int openIPSocket(OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength, OpenFileMode ofm){
	IPSocket *socket = createIPSocket(NULL, transmitIPV4Packet, filterIPV4Packet, receiveIPV4Packet, deleteIPSocket);
	EXPECT(socket != NULL);
	int ok = scanIPSocketArguments(socket, fileName, nameLength);
	EXPECT(ok && ofm.writable);
	ok = startIPSocketTasks(socket);
	EXPECT(ok);
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.setParameter = setIPParameter;
	ff.getParameter = getIPParameter;
	ff.read = readIPSocket;
	ff.write = writeIPSocket;
	ff.close = closeIPSocket;
	completeOpenFile(ofr, socket, &ff);
	return 1;
	// stopIPSocketTasks
	ON_ERROR;
	// wrong argument
	ON_ERROR;
	DELETE(socket);
	ON_ERROR;
	return 0;
}

void internetService(void){
	initIP();
	initUDP();
	initTCP();
	uintptr_t enumDataLink = syncEnumerateFile(resourceTypeToFileName(RESOURCE_DATA_LINK_DEVICE));
	while(1){
		FileEnumeration fe;
		uintptr_t r = enumNextDataLinkDevice(enumDataLink, "*", &fe);
		if(r == IO_REQUEST_FAILURE){
			printk("cannot enum next data link device\n");
			assert(0);
			continue;
		}
		DataLinkDevice *d = createDataLinkDevice(&fe);
		if(d == NULL){
			printk("cannot create IP service for device\n");
			continue;
		}
		addDataLinkDevice(&dataLinkDevList, d);
	}
	printk("warning: IP service error\n");
	syncCloseFile(enumDataLink);
	systemCall_terminate();
}


#ifndef NDEBUG

static void testRWNetwork(const char *fileName, uintptr_t srcAddr, uintptr_t dstAddr, int cnt, int isWrite){
	uintptr_t f = syncOpenFileN(fileName, strlen(fileName), OPEN_FILE_MODE_WRITABLE);
	assert(f != IO_REQUEST_FAILURE);
	//IPV4Address src1 = {bytes: {0, 0, 0, 1}};
	uintptr_t r;
	int i;
	for(i = 0; i < cnt; i++){
		uint8_t buffer[20];
		memset(buffer, i + 10, sizeof(buffer));
		uintptr_t rwSize = sizeof(buffer);

		r = syncSetFileParameter(f, FILE_PARAM_SOURCE_ADDRESS, srcAddr);
		assert(r != IO_REQUEST_FAILURE);
		r = syncSetFileParameter(f, FILE_PARAM_DESTINATION_ADDRESS, dstAddr);
		assert(r != IO_REQUEST_FAILURE);
		{
			uint64_t srcAddr2 = 0;
			r = syncGetFileParameter(f, FILE_PARAM_SOURCE_ADDRESS, &srcAddr2);
			printk("%x %x %x\n",r, srcAddr2, srcAddr);
			assert(r != IO_REQUEST_FAILURE && 0 != (uint32_t)srcAddr2);
		}
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
	ok = waitForFirstResource("udp", RESOURCE_FILE_SYSTEM, matchName);
	assert(ok);
	IPV4Address src0 = ANY_IPV4_ADDRESS;
	//uint16_t srcPort = 60001;
	IPV4Address dst0 = ANY_IPV4_ADDRESS;
	//uint16_t dstPort = 0;

	// wait for device & dhcp
	sleep(1000);
	testRWNetwork("ip:0.0.0.0", src0.value, dst0.value, cnt, isWrite);
	testRWNetwork("udp:0.0.0.0:60000;srcport=60001", src0.value, dst0.value, cnt, isWrite);
	systemCall_terminate();
}

void testWriteIP(void);
void testWriteIP(void){
	testRWIP(30, 1);
}

void testReadIP(void);
void testReadIP(void){
	testRWIP(3, 0);
}
void testIPFileName(void);
void testIPFileName(void){
	const char *name;
	IPV4Address addr;
	IPV4Address subnet;
	uint16_t port;
	uintptr_t pl;
	int r;
	name = "1.2.3.4";
	r = scanIPAddress(name, strlen(name), &addr, NULL, NULL, &pl);
	assert(r == 1);
	assert(r == 1 && pl == (unsigned)strlen(name) && addr.value == 0x04030201);
	name = "1.2.1.2:56";
	r = scanIPAddress(name, strlen(name), &addr, &subnet, &port, &pl);
	assert(r == 2 && pl == (unsigned)strlen(name) && addr.value == 0x02010201 && port == 56);
	name = "3.4.3.4/15:78";
	r = scanIPAddress(name, strlen(name), &addr, &subnet, &port, &pl);
	assert(r == 3 && pl == (unsigned)strlen(name) && addr.value == 0x04030403 && subnet.value == 0x0000feff && port == 78);
	name = "999";
	r = scanPort(name, strlen(name), &port, &pl);
	assert(r == 1 && pl == (unsigned)strlen(name) && port == 999);

	name = "1.2.3.4;src=6.7.8.9:0;dev=i8254x:aa;srcport=1;dstport=2";
	IPSocket s;
	r = scanIPSocketArguments(&s, name, strlen(name));
	// printk("%d %x %d %x %d\n",r, s.remoteAddress.value, s.remotePort, s.localAddress.value, s.localPort);
	assert(r == 1);
	assert(s.remoteAddress.value == 0x04030201 && s.localAddress.value == 0x09080706);
	assert(s.remotePort == 2 && s.localPort == 1);
	assert(s.bindToDevice && isStringEqual(s.deviceName, s.deviceNameLength, "i8254x:aa", strlen("i8254x:aa")));
	printk("test IP file name OK\n");
}

void testCancelRWIP(void);
void testCancelRWIP(void){
	sleep(2000);
	int ok = waitForFirstResource("ip", RESOURCE_FILE_SYSTEM, matchName);
	assert(ok);
	const char *fileName = "ip:0.0.0.0;dev=i8254x:eth0";
	uintptr_t fileHandle = syncOpenFileN(fileName, strlen(fileName), OPEN_FILE_MODE_WRITABLE);
	assert(fileHandle != IO_REQUEST_FAILURE);
	uintptr_t req[20];
	uint8_t buf[4];
	unsigned i;
	for(i = 0; i < LENGTH_OF(req); i++){
		req[i] = systemCall_readFile(fileHandle, buf, sizeof(buf));
		assert(req[i] != IO_REQUEST_FAILURE);
	}
	for(i = 0; i < LENGTH_OF(req); i++){
		unsigned i2 = (i * 7) % LENGTH_OF(req);
		ok = systemCall_cancelIO(req[i2]);
		assert(ok);
		ok = systemCall_cancelIO(req[i2]);
		assert(!ok);
		req[i2] = IO_REQUEST_FAILURE;
	}
	printk("test cancel read IP OK\n");
	syncCloseFile(fileHandle);
	systemCall_terminate();
}

#endif
