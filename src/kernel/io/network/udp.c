#include"common.h"
#include"network.h"

typedef struct{
	uint16_t sourcePort;
	uint16_t destinationPort;
	uint16_t length;
	uint16_t checksum;
	uint8_t payload[];
}UDPHeader;

typedef struct{
	IPV4Header ip;
	UDPHeader udp;
}UDPIPHeader;

static_assert(sizeof(UDPHeader) == 8);

#define MAX_UDP_PAYLOAD_SIZE (MAX_IP_PACKET_SIZE - sizeof(UDPHeader) - sizeof(IPV4Header))

static_assert(MAX_UDP_PAYLOAD_SIZE == 65507);

static uintptr_t getUDPPacketSize(const UDPHeader *h){
	return changeEndian16(h->length);
}

static uintptr_t getUDPDataSize(const UDPHeader *h){
	return getUDPPacketSize(h) - sizeof(*h);
}

typedef struct{
	IPSocket ipSocket;
}UDPSocket;

// the checksum includes data, so initialize header after data

static_assert(sizeof(UDPIPHeader) == 28);
static_assert(MEMBER_OFFSET(typeof(UDPIPHeader), ip) == 0);
static_assert(MEMBER_OFFSET(typeof(UDPIPHeader), udp) == 20);

static void initUDPIPPacket(
	UDPIPHeader *h, const uint8_t *data, uint16_t dataSize,
	IPV4Address localAddr, uint16_t localPort,
	IPV4Address remoteAddr, uint16_t remotePort
){
	initIPV4Header(&h->ip, dataSize + sizeof(h->udp), localAddr, remoteAddr, IP_DATA_PROTOCOL_UDP);
	h->udp.sourcePort = changeEndian16(localPort);
	h->udp.destinationPort = changeEndian16(remotePort);
	h->udp.length = changeEndian16(dataSize + sizeof(h->udp));
	h->udp.checksum = 0;
	memcpy(h->udp.payload, data, dataSize);

	h->udp.checksum = calculateIPDataChecksum(&h->ip);
	if(h->udp.checksum == 0){
		h->udp.checksum = 0xffff;
	}
	assert(calculateIPDataChecksum(&h->ip) == 0);
	assert(getUDPPacketSize(&h->udp) == getIPDataSize(&h->ip));
}

static UDPIPHeader *createUDPIPPacket(
	const uint8_t *data, uintptr_t dataSize,
	IPV4Address localAddr, uint16_t localPort,
	IPV4Address remoteAddr, uint16_t remotePort
){
	if(dataSize > MAX_UDP_PAYLOAD_SIZE){
		return NULL;
	}
	UDPIPHeader *h = allocateKernelMemory(sizeof(*h) + dataSize);
	if(h == NULL){
		return NULL;
	}
	initUDPIPPacket(h, data, dataSize, localAddr, localPort, remoteAddr, remotePort);
	return h;
}

static IPV4Header *createUDPIPPacketFromSocket(
	IPSocket *ips, IPV4Address src, IPV4Address dst,
	const uint8_t *buffer, uintptr_t size
){
	//UDPSocket *udps = ips->instance;
	UDPIPHeader *h = createUDPIPPacket(
		buffer, size,
		src, ips->localPort,
		dst, ips->remotePort
	);
	EXPECT(h != NULL);
	return &h->ip;
	ON_ERROR;
	return NULL;
}

static void deleteUDPIPPacket(IPV4Header *h){
	releaseKernelMemory(h);
}

static int transmitUDPPacket(IPSocket *s){
	return transmitSinglePacket(s, createUDPIPPacketFromSocket, deleteUDPIPPacket);
}

static const UDPHeader *validateUDPHeader(const IPV4Header *packet, uintptr_t packetSize){
	if(packet->protocol != IP_DATA_PROTOCOL_UDP){
		return NULL;
	}
	const uintptr_t ipHeaderSize = getIPHeaderSize(packet);
	if(ipHeaderSize + sizeof(UDPHeader) > packetSize){
		printk("bad UDP/IP packet size %u; IP header size\n", packetSize, ipHeaderSize);
		return NULL;
	}
	const UDPHeader *h = getIPData(packet);
	uintptr_t udpPacketSize = getUDPPacketSize(h);
	if(udpPacketSize != packetSize - ipHeaderSize){
		printk("bad UDP/IP packet size %u; UDP packet size %u; IP header size %u\n", packetSize, udpPacketSize, ipHeaderSize);
		return NULL;
	}
	if(h->checksum != 0 && calculateIPDataChecksum(packet) != 0){
		printk("bad UDP checksum %x; calculated %x\n", h->checksum, calculateIPDataChecksum(packet));
		return NULL;
	}
	return h;
}

static int filterUDPPacket(IPSocket *ips, const IPV4Header *packet, uintptr_t packetSize){
	const UDPHeader *h = validateUDPHeader(packet, packetSize);
	if(h == NULL){
		return 0;
	}
	//UDPSocket *udps = ips->instance;
	if(changeEndian16(h->destinationPort) != ips->localPort){
		return 0;
	}
	return 1;
}

static uintptr_t copyUDPPacketData(uint8_t *buffer, uintptr_t bufferSize, const IPV4Header *packet){
	const UDPHeader *h = getIPData(packet);
	uintptr_t returnSize = getUDPDataSize(h);
	returnSize = MIN(returnSize, bufferSize);
	memcpy(buffer, h->payload, returnSize);
	return returnSize;
}

static int receiveUDPPacket(IPSocket *ips, QueuedPacket *qp){
	return receiveSinglePacket(ips, qp, copyUDPPacketData);
}

static int writeUDP(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){
	UDPSocket *udps = getFileInstance(of);
	return createAddRWIPArgument(udps->ipSocket.transmit, rwfr, &udps->ipSocket, (uint8_t*)buffer, size);
}

static int readUDP(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t size){
	UDPSocket *udps = getFileInstance(of);
	return createAddRWIPArgument(udps->ipSocket.receive, rwfr, &udps->ipSocket, buffer, size);
}

static int getUDPParameter(FileIORequest2 *r2, OpenedFile *of, uintptr_t param){
	UDPSocket *udps = getFileInstance(of);
	uint64_t value = 0;
	int ok = getIPSocketParam(&udps->ipSocket, param, &value);
	if(ok){
		completeFileIO64(r2, value);
	}
	return ok;
}

static int setUDPParameter(FileIORequest2 *r2, OpenedFile *of, uintptr_t param, uint64_t value){
	UDPSocket *udps = getFileInstance(of);
	int ok = 1;
	switch(param){
	/*
	case FILE_PARAM_SOURCE_ADDRESS:
		udps->localPort = ((value >> 32) & 0xffff);
		break;
	case FILE_PARAM_DESTINATION_ADDRESS:
		udps->remotePort= ((value >> 32) & 0xffff);
		break;
	*/
	default:
		ok = 0;
	}
	ok = (ok || setIPSocketParam(&udps->ipSocket, param, value));
	if(ok){
		completeFileIO0(r2);
	}
	return ok;
}

static void closeUDPSocket(CloseFileRequest *cfr, OpenedFile *of){
	UDPSocket *udps = getFileInstance(of);
	stopIPSocketTasks(&udps->ipSocket);
	completeCloseFile(cfr);
}

static void deleteUDPSocket(IPSocket *ips){
	UDPSocket *udps = ips->instance;
	DELETE(udps);
}

static int openUDPSocket(OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength, OpenFileMode ofm){
	UDPSocket *NEW(udps);
	EXPECT(udps != NULL);
	initIPSocket(&udps->ipSocket, udps, transmitUDPPacket, filterUDPPacket, receiveUDPPacket, deleteUDPSocket);
	int ok = scanIPSocketArguments(&udps->ipSocket, fileName, nameLength);
	EXPECT(ok && ofm.writable);
	// TODO: is port using
	ok = startIPSocketTasks(&udps->ipSocket);
	EXPECT(ok);
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.write = writeUDP;
	ff.read = readUDP;
	ff.getParameter = getUDPParameter;
	ff.setParameter = setUDPParameter;
	ff.close = closeUDPSocket;
	completeOpenFile(ofr, udps, &ff);
	return 1;
	// destroyIPSocket
	ON_ERROR;
	ON_ERROR;
	DELETE(udps);
	ON_ERROR;
	return 0;
}

void initUDP(void){
	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openUDPSocket;
	addFileSystem(&fnf, "udp", strlen("udp"));
}
