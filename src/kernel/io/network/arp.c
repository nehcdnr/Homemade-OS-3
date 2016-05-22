#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"ethernet.h"
#include"network.h"

#pragma pack(1)

typedef struct __attribute__((__packed__)){
	uint16_t hardwareType;
	uint16_t protocolType;
	uint8_t hardwareAddressLength;
	uint8_t protocolAddressLength;
	uint16_t operation;
	uint8_t senderHardwareAddress[MAC_ADDRESS_SIZE];
	IPV4Address senderProtocolAddress;
	uint8_t targetHardwareAddress[MAC_ADDRESS_SIZE];
	IPV4Address targetProtocolAddress;
}ARPPacket;

#pragma pack()

static_assert(sizeof(ARPPacket) == 28);

static void initARPReplyPacket(ARPPacket *reply, uint64_t localMACAddress, const ARPPacket *request){
	uint8_t replyMACAddress[MAC_ADDRESS_SIZE];
	toMACAddress(replyMACAddress, localMACAddress);
	reply->hardwareType = TO_BIG_ENDIAN_16(1); // Ethernet
	reply->protocolType = ETHERTYPE_IPV4;
	reply->hardwareAddressLength = MAC_ADDRESS_SIZE;
	reply->protocolAddressLength = sizeof(IPV4Address);
	reply->operation = TO_BIG_ENDIAN_16(2); // reply
	memcpy(reply->senderHardwareAddress, replyMACAddress, MAC_ADDRESS_SIZE);
	reply->senderProtocolAddress = request->targetProtocolAddress;
	memcpy(reply->targetHardwareAddress, request->senderHardwareAddress, MAC_ADDRESS_SIZE);
	reply->targetProtocolAddress = request->senderProtocolAddress;
}

static int checkRequestPacket(const ARPPacket *request, uintptr_t readSize){
	if(readSize < sizeof(*request)){
		return 0;
	}
	if(
		request->hardwareType != TO_BIG_ENDIAN_16(1) ||
		request->protocolType != ETHERTYPE_IPV4 ||
		request->hardwareAddressLength != MAC_ADDRESS_SIZE ||
		request->protocolAddressLength != sizeof(IPV4Address) ||
		request->operation != TO_BIG_ENDIAN_16(1)
	){
		return 0;
	}
	return 1;
}

static void arpService(void *voidArg);

// IMPORVE: the structure is the same as DHCPClient
struct ARPServer{
	uintptr_t deviceFile;
	uint64_t macAddress;
	const IPConfig *ipConfig;
	Spinlock *ipConfigLock;

};

ARPServer *createARPServer(const FileEnumeration *fe, IPConfig *ipConfig, Spinlock *ipConfigLock, uint64_t macAddress){
	ARPServer *NEW(arp);
	arp->deviceFile = syncOpenFileN(fe->name, fe->nameLength, OPEN_FILE_MODE_WRITABLE);
	EXPECT(arp->deviceFile != IO_REQUEST_FAILURE);
	uintptr_t r = syncSetFileParameter(arp->deviceFile, FILE_PARAM_TRANSMIT_ETHERTYPE, ETHERTYPE_ARP);
	EXPECT(r != IO_REQUEST_FAILURE);
	arp->ipConfig = ipConfig;
	arp->ipConfigLock = ipConfigLock;
	arp->macAddress = macAddress;
	Task *t = createSharedMemoryTask(arpService, &arp, sizeof(arp), processorLocalTask());
	EXPECT(t != NULL);
	resume(t);
	return arp;
	//DELETE(t);
	ON_ERROR;
	// set transmit EtherType
	ON_ERROR;
	syncCloseFile(arp->deviceFile);
	ON_ERROR;
	return NULL;
}

// return 0 if internal error (memory, file, ...) occurred
// otherwise, return 1
static int listenARP(ARPServer *arp){
	int dataOK = 1;
	ARPPacket *NEW(request);
	EXPECT(request != NULL);
	ARPPacket *NEW(reply);
	EXPECT(reply != NULL);
	uintptr_t readSize = sizeof(*request);
	uintptr_t r = syncReadFile(arp->deviceFile, request, &readSize);
	EXPECT(r != IO_REQUEST_FAILURE);
	// format
	dataOK = checkRequestPacket(request, readSize);
	EXPECT(dataOK);
	acquireLock(arp->ipConfigLock);
	IPV4Address localAddress = arp->ipConfig->localAddress;
	releaseLock(arp->ipConfigLock);
	// requested IP
	dataOK = (request->targetProtocolAddress.value == localAddress.value);
	EXPECT(dataOK);

	initARPReplyPacket(reply, arp->macAddress, request);
	readSize = sizeof(*reply);
	r = syncWriteFile(arp->deviceFile, reply, &readSize);
	EXPECT(r != IO_REQUEST_FAILURE && readSize == sizeof(*reply));

	DELETE(reply);
	DELETE(request);
	return 1;
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	DELETE(reply);
	ON_ERROR;
	DELETE(request);
	ON_ERROR;
	return (dataOK == 0?1 : 0);
}

static void arpService(void *voidArg){
	ARPServer *arp = *(ARPServer**)voidArg;
	printk("ARP server started\n");
	while(listenARP(arp)){
	}
	systemCall_terminate();
}
