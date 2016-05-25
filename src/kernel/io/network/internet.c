#include"std.h"
#include"memory/memory.h"
#include"resource/resource.h"
#include"task/task.h"
#include"task/exclusivelock.h"
#include"multiprocessor/processorlocal.h"
#include"io/fifo.h"
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
	IPV4Header *h, uint16_t dataLength, IPV4Address localAddr, IPV4Address remoteAddr,
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
	h->source = localAddr;
	h->destination = remoteAddr;

	h->headerChecksum = calculateIPChecksum(h);
	assert(calculateIPChecksum(h) == 0);
}

#define MAX_IP_PAYLOAD_SIZE (MAX_IP_PACKET_SIZE - sizeof(IPV4Header))

static IPV4Header *createIPV4Header(uintptr_t dataLength, IPV4Address localAddr, IPV4Address remoteAddr){
	if(dataLength > MAX_IP_PAYLOAD_SIZE)
		return NULL;
	IPV4Header *h = allocateKernelMemory(sizeof(IPV4Header) + dataLength);
	if(h == NULL){
		return NULL;
	}
	initIPV4Header(h, dataLength, localAddr, remoteAddr, IP_DATA_PROTOCOL_TEST254);
	//memset(h->payload, 0, dataLength);
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
}RWIPQueue;

// TODO: create transmit queue for each socket
// the device file is opened by the internetService() thread,
// so it is not writable by the socket thread
// create only one global transmit queue shared by every socket
static RWIPQueue *globalTransmit = NULL;

static RWIPQueue *createRWIPQueue(void (*taskEntry)(void*)){
	RWIPQueue *NEW(t);
	EXPECT(t != NULL);
	t->argCount = createSemaphore(0);
	EXPECT(t->argCount != NULL);
	t->lock = initialSpinlock;
	t->argList = NULL;
	t->terminateFlag = 0;
	Task *task = createSharedMemoryTask(taskEntry, &t, sizeof(t), processorLocalTask());
	EXPECT(task != NULL);
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
	assert(q != NULL); // call startIPSocket
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

struct QueuedPacket;
static int acceptQueuedPacket(IPSocket *ipSocket, struct QueuedPacket *queuedPacket);
// if terminated, return 0 and *request = NULL
// if q has valid new packet, return 1 and *request = matched RWIPRequest
// if q has invalid new packet, return 1 and *request = NULL
static int matchReadIPRequest(RWIPQueue *q, RWIPRequest **request, struct QueuedPacket *queuedPacket){
	assert(q->argCount != NULL);
	acquireSemaphore(q->argCount);
	acquireLock(&q->lock);
	const int t = q->terminateFlag;
	RWIPRequest *r = q->argList;
	if(t == 0){
		assert(r != NULL);
		if(queuedPacket == NULL || acceptQueuedPacket(r->ipSocket, queuedPacket)){
			REMOVE_FROM_DQUEUE(r);
		}
		else{
			r = NULL;
		}
	}
	releaseLock(&q->lock);
	if(t == 0 && r == NULL){
		releaseSemaphore(q->argCount);
	}
	*request = r;
	return (t == 0);
}

static int nextRWIPArgument(RWIPQueue *q, RWIPRequest **request){
	return matchReadIPRequest(q, request, NULL);
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

int setIPAddress(IPSocket *ips, uintptr_t param, uint64_t value){
	switch(param){
	case FILE_PARAM_SOURCE_ADDRESS:
		setIPSocketLocalAddress(ips, (IPV4Address)(uint32_t)value);
		break;
	case FILE_PARAM_DESTINATION_ADDRESS:
		setIPSocketRemoteAddress(ips, (IPV4Address)(uint32_t)value);
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

static DataLinkDevice *resolveLocalAddress(DataLinkDeviceList *devList, const IPSocket *s, IPV4Address *a){
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

static int transmitIP(const RWIPRequest *arg){
	IPSocket *const s = arg->ipSocket;
	IPV4Address src, dst = s->remoteAddress;
	DataLinkDevice *dld = resolveLocalAddress(&dataLinkDevList, s, &src);
	EXPECT(dld != NULL);
	IPV4Header *packet = arg->ipSocket->createPacket(s, src, dst, arg->buffer, arg->size);
	EXPECT(packet != NULL);
	uintptr_t writeSize = getIPPacketSize(packet);

	EXPECT(writeSize <= dld->mtu);
	uintptr_t r = syncWriteFile(dld->fileHandle, packet, &writeSize);
	EXPECT(r != IO_REQUEST_FAILURE && writeSize == getIPPacketSize(packet));
	arg->ipSocket->deletePacket(packet);
	return 1;
	// cancel write
	ON_ERROR;
	// larger than MTU
	ON_ERROR;
	arg->ipSocket->deletePacket(packet);
	ON_ERROR;
	// no device
	ON_ERROR;
	return 0;
}

static void transmitIPTask(void *voidArg){
	RWIPQueue *tran = *(RWIPQueue**)voidArg;
	while(1){
		RWIPRequest *arg;
		if(nextRWIPArgument(tran, &arg) == 0){
			break;
		}
		int ok = transmitIP(arg);
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
	if(packetSize > readSize){
		printk("IP packet size %u > actual read size %u\n", packetSize, readSize);
		return 0;
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

typedef struct IPFIFO{
	FIFO *fifo;
	struct IPFIFO **prev, *next;
}IPFIFO;

struct IPFIFOList{
	Semaphore *semaphore;
	IPFIFO *head;
};
static struct IPFIFOList readIPFIFOList; // see initInternet()

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

typedef struct QueuedPacket{
	Spinlock lock;
	DataLinkDevice *fromDevice;
	int isBoradcast;
	int referenceCount;
	IPV4Header packet[];
}QueuedPacket;

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
	p->lock = initialSpinlock;
	p->fromDevice = device; // IMPROVE: add reference count if we want to delete device
	acquireLock(&device->ipConfigLock);
	IPV4Address devAddress = device->ipConfig.localAddress, devMask = device->ipConfig.subnetMask;
	releaseLock(&device->ipConfigLock);
	p->isBoradcast = isBroadcastIPV4Address(packet->destination, devAddress, devMask);
	p->referenceCount = 0;
	memcpy(p->packet, packet, packetSize);
	return p;
}

static void addQueuedPacketRef(QueuedPacket *p, int n){
	acquireLock(&p->lock);
	p->referenceCount += n;
	int result = p->referenceCount;
	releaseLock(&p->lock);
	if(result == 0){
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

static void ipDeviceReader(void *voidArg){
	DataLinkDevice *dev = *(DataLinkDevice**)voidArg;
	const uintptr_t fileHandle = dev->fileHandle;
	const uintptr_t mtu = dev->mtu;
	const struct IPFIFOList *const ipFIFOList = &readIPFIFOList;
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

static void receiveIPTask(void *voidArg){
	RWIPQueue *rece = *(RWIPQueue**)voidArg;
	IPFIFO *const ipFIFO = createIPFIFO(64);
	EXPECT(ipFIFO != NULL);
	addToIPFIFOList(&readIPFIFOList, ipFIFO);
	while(1){
		// wait for a valid IPv4 packet
		QueuedPacket *qp = readIPFIFO(ipFIFO);
		const uintptr_t packetSize = getIPPacketSize(qp->packet);
		// match the packet with receiver
		RWIPRequest *arg;
		if(matchReadIPRequest(rece, &arg, qp) == 0){
			addQueuedPacketRef(qp, -1);
			break; // rece->terminateFlag == 1
		}

		if(arg != NULL){
			// truncate packet
			uintptr_t returnSize = arg->size;
			arg->ipSocket->receivePacket(
					arg->ipSocket, arg->buffer,
					&returnSize, qp->packet, packetSize
			);
			completeRWFileIO(arg->rwfr, returnSize, 0);
			DELETE(arg);
		}
		addQueuedPacketRef(qp, -1);
	}
	removeFromIPFIFOList(&readIPFIFOList, ipFIFO);
	deleteIPFIFO(ipFIFO);
	deleteRWIPQueue(rece);
	systemCall_terminate();
	// no return
	ON_ERROR;
	{
		RWIPRequest *arg;
		while(nextRWIPArgument(rece, &arg) != 0){
			completeRWFileIO(arg->rwfr, 0, 0);
			DELETE(arg);
		}
	}
	deleteRWIPQueue(rece);
	systemCall_terminate();
}

void destroyIPSocket(IPSocket *socket){
	if(socket->receive != NULL)
		setIPTaskTerminateFlag(socket->receive);
	// use globalTransmit; see internetService
	//if(socket->transmit != NULL)
	//	setIPTerminateFlag(socket->transmit);
}

static void closeIPSocket(CloseFileRequest *cfr, OpenedFile *of){
	IPSocket *ips = getFileInstance(of);
	destroyIPSocket(ips);
	completeCloseFile(cfr);
	DELETE(ips);
}

void initIPSocket(IPSocket *socket, void *inst, CreatePacket *c, FilterPacket *f, ReceivePacket *r, DeletePacket *d){
	socket->instance = inst;
	socket->localAddress = ANY_IPV4_ADDRESS;
	socket->localPort = 0;
	socket->remoteAddress = ANY_IPV4_ADDRESS;
	socket->remotePort = 0;
	socket->bindToDevice = 0;
	memset(socket->deviceName, 0, sizeof(socket->deviceName));
	socket->deviceNameLength = 0;
	socket->createPacket = c;
	socket->filterPacket = f;
	socket->receivePacket = r;
	socket->deletePacket = d;
	socket->receive = NULL;
	socket->transmit = NULL;
}

static IPSocket *createIPSocket(CreatePacket *c, FilterPacket *f, ReceivePacket *r, DeletePacket *d){
	IPSocket *NEW(s);
	if(s == NULL){
		return NULL;
	}
	initIPSocket(s, s, c, f, r, d);
	return s;
}

int startIPSocket(IPSocket *socket){
	socket->transmit = globalTransmit;
	assert(socket->transmit != NULL);
	socket->receive = createRWIPQueue(receiveIPTask);
	EXPECT(socket->receive != NULL);
	return 1;
	//setIPTaskTerminateFlag(socket->receive);
	// socket->receive = NULL;
	ON_ERROR;
	return 0;
}

void setIPSocketLocalAddress(IPSocket *s, IPV4Address a){
	s->localAddress = a;
}

void setIPSocketRemoteAddress(IPSocket *s, IPV4Address a){
	s->remoteAddress = a;
}
/*
void setIPSocketBindingDevice(IPSocket *s, const char *deviceName, uintptr_t nameLength){
}
*/

static IPV4Header *createIPV4Packet(
	__attribute__((__unused__)) IPSocket *ips, IPV4Address src, IPV4Address dst,
	const uint8_t *buffer, uintptr_t dataSize
){
	IPV4Header *h = createIPV4Header(dataSize, src, dst);
	if(h == NULL){
		return NULL;
	}
	memcpy(((uint8_t*)h) + sizeof(*h), buffer, dataSize);
	return h;
}

static int matchSocketBindingDevice(const IPSocket *s, const DataLinkDevice *d){
	if(s->bindToDevice == 0){
		return 1;
	}
	return isStringEqual(s->deviceName, s->deviceNameLength, d->fileEnumeration.name, d->fileEnumeration.nameLength);
}

int isIPV4PacketAcceptable(const IPSocket *ips, const IPV4Header *packet, int isBroadcast){
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

static int acceptQueuedPacket(IPSocket *s, QueuedPacket *qp){
	if(matchSocketBindingDevice(s, qp->fromDevice) == 0){
		return 0;
	}
	if(isIPV4PacketAcceptable(s, qp->packet, qp->isBoradcast) == 0){
		return 0;
	}
	if(s->filterPacket(s, qp->packet, getIPPacketSize(qp->packet)) == 0){
		return 0;
	}
	return 1;
}

static int filterIPV4Packet(
	__attribute__((__unused__)) IPSocket *ipSocket,
	__attribute__((__unused__)) const IPV4Header *packet,
	__attribute__((__unused__)) uintptr_t packetSize
){
	//see filterQueuedPacket
	return 1;
}

static void copyIPV4Data(
	__attribute__((__unused__)) IPSocket *ips,
	uint8_t *buffer, uintptr_t *bufferSize,
	const IPV4Header *packet, uintptr_t packetSize
){
	// packet header & size are checked in validateIPV4Packet
	const uintptr_t headerSize = getIPHeaderSize(packet);
	*bufferSize = MIN(*bufferSize, packetSize - headerSize); // truncate to buffer size
	memcpy(buffer, ((uint8_t*)packet) + headerSize, *bufferSize);
}

static void deleteIPV4Packet(IPV4Header *h){
	releaseKernelMemory(h);
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
// return number of parsed characters if success
// return 0 otherwise
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
	IPSocket *socket = createIPSocket(createIPV4Packet, filterIPV4Packet, copyIPV4Data, deleteIPV4Packet);
	EXPECT(socket != NULL);
	int ok = scanIPSocketArguments(socket, fileName, nameLength);
	EXPECT(ok && ofm.writable);
	ok = startIPSocket(socket);
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
	// wrong argument
	ON_ERROR;
	DELETE(socket);
	ON_ERROR;
	return 0;
}

void internetService(void){
	if(initIPFIFOList(&readIPFIFOList) == 0){
		panic("cannot initialize IP FIFO");
	}
	globalTransmit = createRWIPQueue(transmitIPTask);
	if(globalTransmit == NULL){
		panic("cannot initialize IP transmit queue");
	}

	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openIPSocket;
	if(addFileSystem(&fnf, "ip", strlen("ip")) == 0){
		panic("cannot create IP service");
	}
	initUDP();
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
	setIPTaskTerminateFlag(globalTransmit);
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
	ok = waitForFirstResource("udp", RESOURCE_FILE_SYSTEM, matchName);
	assert(ok);
	IPV4Address src0 = ANY_IPV4_ADDRESS;
	uint16_t srcPort = 60001;
	IPV4Address dst0 = ANY_IPV4_ADDRESS;
	uint16_t dstPort = 0;

	// wait for device
	sleep(1000);
	testRWNetwork("ip:0.0.0.0", src0.value, dst0.value, cnt, isWrite);
	testRWNetwork("udp:0.0.0.0:60000;srcport=60001",
		src0.value + (((uint64_t)srcPort) << 32),
		dst0.value + (((uint64_t)dstPort) << 32), cnt, isWrite);
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

#endif
