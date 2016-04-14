#include"common.h"
#include"file/file.h"
#include"network.h"

typedef struct{
	IPV4Header ip;
	uint16_t sourcePort;
	uint16_t destinationPort;
	uint16_t length;
	uint16_t checksum;
	uint8_t payload[];
}UDPIPHeader;

#define UDP_HEADER_SIZE (sizeof(UDPIPHeader) - sizeof(IPV4Header))

static_assert(UDP_HEADER_SIZE == 8);
static_assert(sizeof(UDPIPHeader) == 28);

#define MAX_UDP_PAYLOAD_SIZE (MAX_IP_PACKET_SIZE - sizeof(UDPIPHeader))

static_assert(MAX_UDP_PAYLOAD_SIZE == 65507);

static uint16_t calculateUDPChecksum(UDPIPHeader *h){
	// pseudo ip header
	uint32_t cs = calculatePseudoIPChecksum(&h->ip);
	// udp header + udp data
	const uintptr_t udpLength = changeEndian16(h->length);
	assert(getIPDataSize(&h->ip) == udpLength);
	uintptr_t i;
	for(i = 0; i * 2 < udpLength; i++){
		cs += (uint32_t)changeEndian16(((uint16_t*)h)[i]);
	}
	// padding
	if(udpLength % 2 != 0){
		uint16_t lastByte = (uint16_t)((uint8_t*)h)[udpLength - 1];
		cs += changeEndian16(lastByte);
	}
	while(cs > 0xffff){
		cs = (cs & 0xffff) + (cs >> 16);
	}
	cs = changeEndian16(cs ^ 0xffff);
	return (cs == 0? 0xffff: cs);
}

typedef struct{
	IPSocket ipSocket;
	uint16_t sourcePort;
	uint16_t destinationPort;
}UDPSocket;

// the checksum includes data, so do not separate header and data initialization
static UDPIPHeader *_createUDPIPPacket(
	const uint8_t *data, uint16_t dataLength,
	IPV4Address srcAddress, uint16_t srcPort,
	IPV4Address dstAddress, uint16_t dstPort
){
	if(dataLength > MAX_UDP_PAYLOAD_SIZE){
		return NULL;
	}
	UDPIPHeader *h = allocateKernelMemory(sizeof(*h) + dataLength);
	if(h == NULL){
		return NULL;
	}
	initIPV4Header(&h->ip, dataLength + UDP_HEADER_SIZE, srcAddress, dstAddress, IP_DATA_PROTOCOL_UDP);
	h->sourcePort = changeEndian16(srcPort);
	h->destinationPort = changeEndian16(dstPort);
	h->length = changeEndian16(dataLength + UDP_HEADER_SIZE);
	h->checksum = 0;
	memcpy(h->payload, data, dataLength);

	h->checksum = calculateUDPChecksum(h);
	assert(calculateUDPChecksum(h) == 0xffff);
	return h;
}

static IPV4Header *createUDPIPPacket(IPSocket *ips, const uint8_t *buffer, uintptr_t size){
	UDPSocket *udps = ips->instance;
	UDPIPHeader *h = _createUDPIPPacket(
		buffer, size,
		ips->source, udps->sourcePort,
		ips->destination, udps->destinationPort
	);
	EXPECT(h != NULL);
	return &h->ip;
	ON_ERROR;
	return NULL;
}

static void deleteUDPIPPacket(IPV4Header *h){
	releaseKernelMemory(h);
}

static int writeUDP(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){
	UDPSocket *udps = getFileInstance(of);
	return createAddRWIPArgument(udps->ipSocket.transmit, rwfr, &udps->ipSocket, (uint8_t*)buffer, size);
}

static int setUDPParameter(FileIORequest2 *r2, OpenedFile *of, uintptr_t param, uint64_t value){
	UDPSocket *udps = getFileInstance(of);
	int ok = 1;
	switch(param){
	case FILE_PARAM_SOURCE_ADDRESS:
		udps->sourcePort = ((value >> 32) & 0xffff);
		break;
	case FILE_PARAM_DESTINATION_ADDRESS:
		udps->destinationPort= ((value >> 32) & 0xffff);
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
	int ok = initIPSocket(&udps->ipSocket, udps, src, createUDPIPPacket, NULL/*TODO:validateUDPPAcket*/, deleteUDPIPPacket);
	EXPECT(ok);
	udps->sourcePort = src[4];
	udps->destinationPort = 0;
	// TODO: is port using
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.write = writeUDP;
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
