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

static_assert(sizeof(UDPIPHeader) == 28);

#define MAX_UDP_PAYLOAD_SIZE (MAX_IP_PACKET_SIZE - sizeof(UDPIPHeader))

static_assert(MAX_UDP_PAYLOAD_SIZE == 65507);

/*
static uint16_t calculateUDPChecksum(UDPIPHeader *h){
	// pseudo ip header
	uint32_t cs = calculatePseudoIPChecksum(&h->ip);
	// udp header + udp data
	const uintptr_t udpLength = getIPDataSize(&h->ip);
	uintptr_t i;
	for(i = 0; i * 2 < udpLength; i++){
		cs += (uint32_t)changeEndian16(((uint16_t*)h)[i]);
	}
	// padding
	if(udpLength % 2 != 0){
		uint16_t lastByte = (uint16_t)((uint8_t*)h)[udpLength - 1];
		cs += (lastByte << 8);
	}
	while(cs > 0xffff){
		cs = (cs & 0xffff) + (cs >> 16);
	}
	cs = changeEndian16(cs ^ 0xffff);
	return (cs == 0? 0xffff: cs);
}

static UDPIPHeader *createUDPPacket(
	uint16_t dataLength,
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
	initIPV4Header(&h->ip, dataLength + sizeof(UDPIPHeader), srcAddress, dstAddress, IP_DATA_PROTOCOL_UDP);
	h->sourcePort = changeEndian16(srcPort);
	h->destinationPort = changeEndian16(dstPort);
	h->length = dataLength + sizeof(UDPIPHeader);
	h->checksum = 0;
	h->checksum = calculateUDPChecksum(h);
	assert(calculateUDPChecksum(h) == 0);
	return h;
}
*/
typedef struct{
	IPSocket ipSocket;
	uint16_t port;
}UDPSocket;
/*
int writeUDP(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){

}
*/
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
	int ok = initIPSocket(&udps->ipSocket, src);
	EXPECT(ok);
	udps->port = src[4];
	// TODO: is port using
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	// TODO: ff.write = writeUDP;
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
