#include"common.h"
#include"file/file.h"
#include"network.h"

typedef struct{
	uint16_t sourcePort;
	uint16_t destinationPort;
	uint16_t length;
	uint16_t checksum;
	uint8_t payload[];
}UDPHeader;

static_assert(sizeof(UDPHeader) == 8);

#define MAX_UDP_PAYLOAD_SIZE (MAX_IP_PACKET_SIZE - sizeof(UDPHeader) - sizeof(IPV4Header))

static_assert(MAX_UDP_PAYLOAD_SIZE == 65507);

static uintptr_t getUDPPacketSize(const UDPHeader *h){
	return changeEndian16(h->length);
}

static uintptr_t getUDPDataSize(const UDPHeader *h){
	return getUDPPacketSize(h) - sizeof(*h);
}

static uint16_t calculateUDPChecksum(const IPV4Header *ip, const UDPHeader *h){
	// pseudo ip header
	uint32_t cs = calculatePseudoIPChecksum(ip);
	// udp header + udp data
	const uintptr_t udpLength = getUDPPacketSize(h);
	assert(getIPDataSize(ip) == udpLength);
	uintptr_t i;
	for(i = 0; i * 2 + 1 < udpLength; i++){
		cs += (uint32_t)changeEndian16(((uint16_t*)h)[i]);
	}
	// padding
	if(udpLength % 2 != 0){
		uint16_t lastByte = (uint16_t)((uint8_t*)h)[udpLength - 1];
		cs += (uint32_t)changeEndian16(lastByte);
	}
	while(cs > 0xffff){
		cs = (cs & 0xffff) + (cs >> 16);
	}
	cs = changeEndian16(cs ^ 0xffff);
	return cs;
}

typedef struct{
	IPSocket ipSocket;
	uint16_t localPort;
	uint16_t remotePort;
}UDPSocket;

// the checksum includes data, so do not separate header and data initialization
static IPV4Header *_createUDPIPPacket(
	const uint8_t *data, uint16_t dataLength,
	IPV4Address localAddr, uint16_t localPort,
	IPV4Address remoteAddr, uint16_t remotePort
){
	if(dataLength > MAX_UDP_PAYLOAD_SIZE){
		return NULL;
	}
	struct{
		IPV4Header ip;
		UDPHeader udp;
	}*h = allocateKernelMemory(sizeof(*h) + dataLength);
	static_assert(sizeof(*h) == 28);
	static_assert(MEMBER_OFFSET(typeof(*h), ip) == 0);
	if(h == NULL){
		return NULL;
	}
	initIPV4Header(&h->ip, dataLength + sizeof(h->udp), localAddr, remoteAddr, IP_DATA_PROTOCOL_UDP);
	h->udp.sourcePort = changeEndian16(localPort);
	h->udp.destinationPort = changeEndian16(remotePort);
	h->udp.length = changeEndian16(dataLength + sizeof(h->udp));
	h->udp.checksum = 0;
	memcpy(h->udp.payload, data, dataLength);

	h->udp.checksum = calculateUDPChecksum(&h->ip, &h->udp);
	if(h->udp.checksum == 0){
		h->udp.checksum = 0xffff;
	}
	assert(calculateUDPChecksum(&h->ip, &h->udp) == 0);
	return &h->ip;
}

static IPV4Header *createUDPIPPacket(IPSocket *ips, const uint8_t *buffer, uintptr_t size){
	UDPSocket *udps = ips->instance;
	IPV4Header *h = _createUDPIPPacket(
		buffer, size,
		ips->localAddress, udps->localPort,
		ips->remoteAddress, udps->remotePort
	);
	EXPECT(h != NULL);
	return h;
	ON_ERROR;
	return NULL;
}

static void deleteUDPIPPacket(IPV4Header *h){
	releaseKernelMemory(h);
}

static UDPHeader *validateUDPHeader(const IPV4Header *packet, uintptr_t packetSize){
	if(packet->protocol != IP_DATA_PROTOCOL_UDP){
		return NULL;
	}
	const uintptr_t ipHeaderSize = getIPHeaderSize(packet);
	if(ipHeaderSize + sizeof(UDPHeader) > packetSize){
		printk("bad UDP/IP packet size %u; IP header size\n", packetSize, ipHeaderSize);
		return NULL;
	}
	UDPHeader *h = (UDPHeader*)(((uint8_t*)packet) + ipHeaderSize);
	uintptr_t udpPacketSize = getUDPPacketSize(h);
	if(udpPacketSize != packetSize - ipHeaderSize){
		printk("bad UDP/IP packet size %u; UDP packet size %u; IP header size %u\n", packetSize, udpPacketSize, ipHeaderSize);
		return NULL;
	}
	if(h->checksum != 0 && calculateUDPChecksum(packet, h) != 0){
		printk("bad UDP checksum %x %x\n", h->checksum, calculateUDPChecksum(packet, h) );
		return NULL;
	}
	return h;
}

static int copyUDPData(
	IPSocket *ips,
	uint8_t *buffer, uintptr_t *bufferSize,
	const IPV4Header *packet, uintptr_t packetSize
){
	if(isIPV4PacketAcceptable(ips, packet) == 0){
		return 0;
	}
	UDPHeader *h = validateUDPHeader(packet, packetSize);
	if(h == NULL){
		return 0;
	}
	UDPSocket *udps = ips->instance;
	if(changeEndian16(h->destinationPort) != udps->localPort){
		return 0;
	}
	// TODO:check h->sourcePort

	uintptr_t udpDataSize = getUDPDataSize(h);
	*bufferSize = MIN(udpDataSize, *bufferSize);
	memcpy(buffer, h->payload, *bufferSize);
	return 1;
}

static int writeUDP(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){
	UDPSocket *udps = getFileInstance(of);
	return createAddRWIPArgument(udps->ipSocket.transmit, rwfr, &udps->ipSocket, (uint8_t*)buffer, size);
}

static int readUDP(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t size){
	UDPSocket *udps = getFileInstance(of);
	return createAddRWIPArgument(udps->ipSocket.receive, rwfr, &udps->ipSocket, buffer, size);
}

static int setUDPParameter(FileIORequest2 *r2, OpenedFile *of, uintptr_t param, uint64_t value){
	UDPSocket *udps = getFileInstance(of);
	int ok = 1;
	switch(param){
	case FILE_PARAM_SOURCE_ADDRESS:
		udps->localPort = ((value >> 32) & 0xffff);
		break;
	case FILE_PARAM_DESTINATION_ADDRESS:
		udps->remotePort= ((value >> 32) & 0xffff);
		break;
	default:
		ok = 0;
	}
	ok = (ok || setIPAddress(&udps->ipSocket, param, value));
	if(ok){
		completeFileIO0(r2);
	}
	return ok;
}

static void closeUDPSocket(CloseFileRequest *cfr, OpenedFile *of){
	UDPSocket *udps = getFileInstance(of);
	destroyIPSocket(&udps->ipSocket);
	completeCloseFile(cfr);
	DELETE(udps);
}

static int openUDPSocket(OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength, OpenFileMode ofm){
	unsigned src[5] = {0, 0, 0, 0, 0};
	int scanCount = snscanf(fileName, nameLength, "%u.%u.%u.%u:%u", src + 0, src + 1, src + 2, src + 3, src + 4);
	EXPECT(scanCount == 5 && ofm.writable);

	UDPSocket *NEW(udps);
	EXPECT(udps != NULL);
	int ok = initIPSocket(&udps->ipSocket, udps, src, createUDPIPPacket, copyUDPData, deleteUDPIPPacket);
	EXPECT(ok);
	udps->localPort = src[4];
	udps->remotePort = 0;
	// TODO: is port using
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.write = writeUDP;
	ff.read = readUDP;
	ff.setParameter = setUDPParameter;
	ff.close = closeUDPSocket;
	completeOpenFile(ofr, udps, &ff);
	return 1;
	// destroyIPSocket
	ON_ERROR;
	DELETE(udps);
	ON_ERROR;
	ON_ERROR;
	return 0;
}

void initUDP(void){
	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openUDPSocket;
	addFileSystem(&fnf, "udp", strlen("udp"));
}
