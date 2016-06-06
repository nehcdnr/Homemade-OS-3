#include"common.h"
#include"std.h"
#include"file/file.h"
#include"network.h"

#pragma pack(1)

typedef struct TCPHeader{
	uint16_t sourcePort;
	uint16_t destinationPort;
	uint32_t sequenceNumber;
	uint32_t acknowledgeNumber;
	uint16_t dataOffset: 4;
	uint16_t reserved: 3;
	uint16_t ns: 1; // nonce sum
	uint16_t cwr: 1; // congestion window reduced
	uint16_t ece: 1; // ECN echo
	uint16_t urg: 1; // urgent pointer effective
	uint16_t ack: 1;
	uint16_t psh: 1; // push
	uint16_t rst: 1; // reset
	uint16_t syn: 1; // synchronize sequence number
	uint16_t fin: 1; // finish
	uint16_t windowsSize;
	uint16_t checksum;
	uint16_t urgentPointer;
	uint8_t options[];
	// uint8_t payload[];
}__attribute__((__packed__)) TCPHeader;

#pragma pack()

static_assert(sizeof(TCPHeader) == 20);

static uintptr_t getTCPHeaderSize(const TCPHeader *h){
	return sizeof(uint32_t) * ((uintptr_t)h->dataOffset);
}

static void *getTCPData(const TCPHeader *h){
	return ((uint8_t*)h) + getTCPHeaderSize(h);
}
__attribute__((__unused__))
static uintptr_t getTCPDataSize(IPV4Header *ip, TCPHeader *tcp){
	return getIPDataSize(ip) - getTCPHeaderSize(tcp);
}

typedef struct{
	IPV4Header ip;
	TCPHeader tcp;
}TCPIPHeader;

static void initTCPIPPacket(
	TCPIPHeader *h, const uint8_t *data, uint16_t dataSize,
	uint32_t seqNumber, uint16_t windowSize,
	IPV4Address localAddr, uint16_t localPort,
	IPV4Address remoteAddr, uint16_t remotePort
){
	initIPV4Header(&h->ip, dataSize, localAddr, remoteAddr, IP_DATA_PROTOCOL_TCP);
	TCPHeader *tcp = &h->tcp;
	MEMSET0(tcp);
	tcp->sourcePort = changeEndian16(localPort);
	tcp->destinationPort = changeEndian16(remotePort);
	tcp->sequenceNumber = changeEndian32(seqNumber);
	//acknowledgeNumber
	tcp->dataOffset = sizeof(TCPHeader) / sizeof(uint32_t);
	/*
	reserved
	ns
	cwr
	ece
	urg
	ack
	psh
	rst
	syn
	fin
	*/
	tcp->windowsSize = changeEndian16(windowSize);
	tcp->checksum = 0;
	//urgentPointer;
	//options
	memcpy(getTCPData(tcp), data, dataSize);
	tcp->checksum = calculateIPDataChecksum(&h->ip);
	assert(calculateIPDataChecksum(&h->ip) == 0);
}
__attribute__((__unused__))
static TCPIPHeader *createTCPIPPacket(
	const uint8_t *data, uint16_t dataSize,
	uint32_t seqNumber, uint16_t windowSize,
	IPV4Address localAddr, uint16_t localPort,
	IPV4Address remoteAddr, uint16_t remotePort
){
	TCPIPHeader *NEW(h);
	if(h == NULL){
		return NULL;
	}
	initTCPIPPacket(h, data, dataSize, seqNumber, windowSize, localAddr, localPort, remoteAddr, remotePort);
	return h;
}

typedef struct{
	IPSocket ipSocket;
}TCPSocket;
/*
static void TCPClientTask(void *arg){
	TCPSocket *tcps = *(TCPSocket**)arg;

}
*/
static void closeTCP(CloseFileRequest *cfr, OpenedFile *of){
	TCPSocket *tcps = getFileInstance(of);
	stopIPSocketTasks(&tcps->ipSocket);
	completeCloseFile(cfr);
}

static void deleteTCPSocket(IPSocket *ips){
	TCPSocket *tcps = ips->instance;
	DELETE(tcps);
}

static int openTCPClient(OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength, OpenFileMode ofm){
	TCPSocket *NEW(tcps);
	EXPECT(tcps != NULL);
	initIPSocket(&tcps->ipSocket, tcps, NULL, NULL, NULL, NULL, deleteTCPSocket);
	int ok = scanIPSocketArguments(&tcps->ipSocket, fileName, nameLength);
	EXPECT(ok && ofm.writable);

	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.close = closeTCP;
	//Task *t = createSharedMemoryTask(TCPClientTask);
	completeOpenFile(ofr, tcps, &ff);
	return 1;
	ON_ERROR;
	DELETE(tcps);
	ON_ERROR;
	return 0;
}

void initTCP(void){
	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openTCPClient;
	addFileSystem(&fnf, "tcpclient", strlen("tcpclient"));
	//fnf.open = openTCPServer;
	//addFileSystem(&fnf, "tcpserver", strlen("tcpserver"));
}
