#include"common.h"
#include"std.h"
#include"file/file.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"io/fifo.h"
#include"network.h"

#pragma pack(1)

typedef union{
	struct{
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
	};
	uint16_t value;
}TCPFlags;

typedef struct TCPHeader{
	uint16_t sourcePort;
	uint16_t destinationPort;
	uint32_t sequenceNumber;
	uint32_t acknowledgeNumber;
	TCPFlags flags;
	uint16_t windowsSize;
	uint16_t checksum;
	uint16_t urgentPointer;
	uint8_t options[];
	// uint8_t payload[];
}__attribute__((__packed__)) TCPHeader;

#pragma pack()

static_assert(sizeof(TCPFlags) == 2);
static_assert(sizeof(TCPHeader) == 20);

static uintptr_t getTCPHeaderSize(const TCPHeader *h){
	return sizeof(uint32_t) * ((uintptr_t)h->flags.dataOffset);
}

static void *getTCPData(const TCPHeader *h){
	return ((uint8_t*)h) + getTCPHeaderSize(h);
}
/*
static uintptr_t getTCPDataSize(IPV4Header *ip, TCPHeader *tcp){
	return getIPDataSize(ip) - getTCPHeaderSize(tcp);
}
*/
typedef struct{
	IPV4Header ip;
	TCPHeader tcp;
}TCPIPHeader;

// if data == NULL then initialize the header but do not copy the data
// flags.dataOffset and flags.reserved are ignored
static void initTCPPacket(
	TCPHeader *h, const uint8_t *data, uint16_t dataSize,
	uint32_t seqNumber, uint32_t ackNumber, uint16_t windowSize, TCPFlags flags,
	IPV4Address localAddr, uint16_t localPort,
	IPV4Address remoteAddr, uint16_t remotePort
){
	TCPHeader *tcp = h;
	// do not modify data here
	// memset(tcp, 0, sizeof(TCPHeader));
	tcp->sourcePort = changeEndian16(localPort);
	tcp->destinationPort = changeEndian16(remotePort);
	tcp->sequenceNumber = changeEndian32(seqNumber);
	tcp->acknowledgeNumber = changeEndian32(ackNumber);
	tcp->flags = flags;
	tcp->flags.reserved = 0;
	tcp->flags.dataOffset = sizeof(TCPHeader) / sizeof(uint32_t);
	tcp->windowsSize = changeEndian16(windowSize);
	tcp->checksum = 0;
	tcp->urgentPointer = 0;
	//options
	if(data != NULL){
		memcpy(getTCPData(tcp), data, dataSize);
	}
	tcp->checksum = calculateIPDataChecksum2(data, dataSize, localAddr, remoteAddr, IP_DATA_PROTOCOL_TCP);
	assert(calculateIPDataChecksum2(data, dataSize, localAddr, remoteAddr, IP_DATA_PROTOCOL_TCP) == 0);
}

typedef struct{
	uint32_t sequenceBegin;
	uintptr_t sequenceLength;
	uint8_t *buffer;
	uintptr_t size;
}TCPWindow;

typedef struct{
	// the receiver adjusts both of the windows
	// receiver ACK number = transmitWindow.sequenceEnd
	// transmitter ACK number = receiveWindow.sequenceBegin
	//TCPWindow receiveWindow, transmitWindow;
	// IP packet from TCP thread to IP transmit thread
	uintptr_t rawSocketHandle;
	// RWTCPRequest from user thread to TCP thread
	uintptr_t rwRequestFIFOHandle;
	// read buffer for rawSocketHandle
	TCPHeader *rawBuffer;
	uintptr_t rawBufferSize;
	IPV4Address localAddress, remoteAddress;
	uint16_t localPort, remotePort;
}TCPSocket;

typedef struct{
	RWFileRequest *rwfr; // if rwfr == NULL, the socket is closing
	uint8_t *buffer;
	uintptr_t bufferSize;
	int isWrite;
}RWTCPRequest;

static int tcpHandShake(TCPSocket *tcps){
	TCPFlags f;
	f.value = 0;
	f.syn = 1;
	initTCPPacket(
		tcps->rawBuffer, NULL, 0,
		0, 0, 8192, f,
		tcps->localAddress, tcps->localPort,
		tcps->remoteAddress, tcps->remotePort
	);
	uintptr_t r = systemCall_writeFile(tcps->rawSocketHandle, tcps->rawBuffer, getTCPHeaderSize(tcps->rawBuffer));
	if(r == IO_REQUEST_FAILURE)
		printk("TCP handshake 1 error");
	// TODO:
	while(1)
		sleep(1);
	return 1;
}

static int tcpLoop(TCPSocket *tcps){
	uintptr_t readSocketIO = IO_REQUEST_FAILURE;
	uintptr_t readFIFOIO = IO_REQUEST_FAILURE;
	uintptr_t timerIO = IO_REQUEST_FAILURE;
	while(1){
		RWTCPRequest rwTCPRequest;

		if(readSocketIO == IO_REQUEST_FAILURE){
			readSocketIO = systemCall_readFile(tcps->rawSocketHandle, tcps->rawBuffer, tcps->rawBufferSize);
		}
		if(readFIFOIO == IO_REQUEST_FAILURE){
			readFIFOIO = systemCall_readFile(tcps->rwRequestFIFOHandle, &rwTCPRequest, sizeof(rwTCPRequest));
		}
		if(timerIO == IO_REQUEST_FAILURE){
			timerIO = systemCall_setAlarm(3000, 0);
		}

		uintptr_t readSize;
		uintptr_t r = systemCall_waitIOReturn(UINTPTR_NULL, 1, &readSize);
		if(r == IO_REQUEST_FAILURE){
			printk("warning: TCP task failed to wait IO\n");
			continue;
		}
		if(r == readSocketIO){
			readSocketIO = IO_REQUEST_FAILURE;
			//TODO:
			continue;
		}
		if(r == readFIFOIO){
			readFIFOIO = IO_REQUEST_FAILURE;
			if(readSize != sizeof(rwTCPRequest)){
				printk("warning: wrong size of RWTCPRequest %x\n", readSize);
				continue;
			}
			if(rwTCPRequest.rwfr == NULL){
				// socket is closing
				return 0;
			}
			//TODO:
			continue;
		}
		if(r == timerIO){
			timerIO = IO_REQUEST_FAILURE;
			//TODO:
			continue;
		}
		printk("warning: unknown IO handle %x in TCP task\n", r);
	}
}

static TCPSocket *createTCPSocket(const char *fileName, uintptr_t nameLength){
	const char *tcpraw = "tcpraw:";
	const uintptr_t tcpRawNameLength = nameLength + strlen(tcpraw);
	char *tcpRawName = allocateKernelMemory(sizeof(char) * tcpRawNameLength);
	EXPECT(tcpRawName != NULL);
	snprintf(tcpRawName, tcpRawNameLength, "%s%s", tcpraw, fileName);

	TCPSocket *NEW(tcps);
	EXPECT(tcps != NULL);
	tcps->rwRequestFIFOHandle = syncOpenFIFOFile();
	EXPECT(tcps->rwRequestFIFOHandle != IO_REQUEST_FAILURE);
	tcps->rawSocketHandle = syncOpenFileN(tcpRawName, tcpRawNameLength, OPEN_FILE_MODE_WRITABLE);
	EXPECT(tcps->rawSocketHandle != IO_REQUEST_FAILURE);
	// get parameters
	enum FileParameter param[5] = {
		FILE_PARAM_MAX_WRITE_SIZE,
		FILE_PARAM_SOURCE_ADDRESS, FILE_PARAM_DESTINATION_ADDRESS,
		FILE_PARAM_SOURCE_PORT, FILE_PARAM_DESTINATION_PORT
	};
	uint64_t getValue[5] = {0, 0, 0, 0, 0};
	int i;
	for(i = 0; i < 5; i++){
		uintptr_t r = syncGetFileParameter(tcps->rawSocketHandle, param[i], getValue + i);
		if(r == IO_REQUEST_FAILURE){
			break;
		}
	}
	EXPECT(i >= 5);
	tcps->rawBufferSize = (uintptr_t)getValue[0];
	tcps->localAddress.value = (uint32_t)getValue[1];
	tcps->remoteAddress.value = (uint32_t)getValue[2];
	tcps->localPort = (uint16_t)getValue[3];
	tcps->remotePort = (uint16_t)getValue[4];
	// allocate a buffer for R/W
	tcps->rawBuffer = allocateKernelMemory(tcps->rawBufferSize);
	EXPECT(tcps->rawBuffer != NULL);

	releaseKernelMemory(tcpRawName);
	return tcps;
	// releaseKernelMemory(tcps->rawBuffer);
	ON_ERROR;
	// get parameter
	ON_ERROR;
	syncCloseFile(tcps->rawSocketHandle);
	ON_ERROR;
	syncCloseFile(tcps->rwRequestFIFOHandle);
	ON_ERROR;
	DELETE(tcps);
	ON_ERROR;
	releaseKernelMemory(tcpRawName);
	ON_ERROR;
	return 0;
}

static void deleteTCPSocket(TCPSocket *tcps){
	uintptr_t r;
	r = syncCloseFile(tcps->rwRequestFIFOHandle);
	if(r == IO_REQUEST_FAILURE){
		printk("warning: cannot close TCP request FIFO\n");
	}
	r = syncCloseFile(tcps->rawSocketHandle);
	if(r == IO_REQUEST_FAILURE){
		printk("warning: cannot close TCP raw socket\n");
	}
	releaseKernelMemory(tcps->rawBuffer);
	DELETE(tcps);
}

static void closeTCP(CloseFileRequest *cfr, OpenedFile *of){
	TCPSocket *tcps = getFileInstance(of);
	printk("close TCP %x", tcps);
	//TODO: send NULL request to tcps->rwRequestFIFOHandle;
	completeCloseFile(cfr);
}

struct TCPTaskArgument{
	OpenFileRequest *ofr;
	const char *fileName;
	uintptr_t nameLength;
};

static void tcpTask(void *voidArg){
	struct TCPTaskArgument *arg = (struct TCPTaskArgument *)voidArg;

	TCPSocket *tcps = createTCPSocket(arg->fileName, arg->nameLength);
	EXPECT(tcps != NULL);
	tcpHandShake(tcps);

	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	//ff.read = readTCP;
	//ff.write = writeTCP;
	ff.close = closeTCP;
	completeOpenFile(arg->ofr, tcps, &ff);

	while(tcpLoop(tcps)){
	}

	deleteTCPSocket(tcps);
	systemCall_terminate();
	// deleteTCPSocket
	ON_ERROR;
	systemCall_terminate();
}


Task *getIPServiceTask(void);

static int openTCPClient(OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength, OpenFileMode ofm){
	if(ofm.writable == 0){
		return 0;
	}
	struct TCPTaskArgument arg = {ofr, fileName, nameLength};
	Task *t = createSharedMemoryTask(tcpTask, &arg, sizeof(arg), processorLocalTask());
	if(t == NULL){
		return 0;
	}
	resume(t);
	return 1;
}

typedef struct{
	IPSocket ipSocket;
}TCPRawSocket;

static int getTCPRawParameter(FileIORequest2 *r2, OpenedFile *of, enum FileParameter fp){
	TCPRawSocket *tcps = getFileInstance(of);
	uint64_t value;
	int ok = getIPSocketParam(&tcps->ipSocket, fp, &value);
	if(ok){
		completeFileIO64(r2, value);
	}
	return ok;
}

static int readTCPRawSocket(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t size){
	TCPRawSocket *tcps = getFileInstance(of);
	return createAddRWIPArgument(tcps->ipSocket.receive, rwfr, &tcps->ipSocket, buffer, size);
}

static int writeTCPRawSocket(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){
	TCPRawSocket *tcps = getFileInstance(of);
	return createAddRWIPArgument(tcps->ipSocket.transmit, rwfr, &tcps->ipSocket, (uint8_t*)buffer, size);
}

static void closeTCPRawSocket(CloseFileRequest *cfr, OpenedFile *of){
	TCPRawSocket *tcps = getFileInstance(of);
	stopIPSocketTasks(&tcps->ipSocket);
	completeCloseFile(cfr);
}


static const TCPHeader *validateTCPPacket(const IPV4Header *packet, uintptr_t packetSize){
	if(packet->protocol != IP_DATA_PROTOCOL_TCP){
		return NULL;
	}
	const uintptr_t ipHeaderSize = getIPHeaderSize(packet);
	if(ipHeaderSize + sizeof(TCPHeader) > packetSize){
		printk("bad TCP/IP packet size %u; IP header size %u\n", packetSize, ipHeaderSize);
		return NULL;
	}
	const TCPHeader *h = getIPData(packet);
	const uintptr_t tcpHeaderSize = getTCPHeaderSize(h);
	if(ipHeaderSize + tcpHeaderSize > packetSize){
		printk("bad TCP/IP packet size %u; TCP header size %u; IP header size %u",
			packetSize, tcpHeaderSize, ipHeaderSize);
		return NULL;
	}
	if(calculateIPDataChecksum(packet) != 0){
		printk("bad checksum %x; calculated %x", h->checksum, calculateIPDataChecksum(packet));
		return NULL;
	}
	return h;
}

static int filterTCPPacket(IPSocket *ips, const IPV4Header *packet, uintptr_t packetSize){
	const TCPHeader *h = validateTCPPacket(packet, packetSize);
	if(h == NULL){
		return 0;
	}
	//TCPRawSocket tcps = ips->instance;
	if(
		ips->remotePort != changeEndian16(h->sourcePort) ||
		ips->localPort != changeEndian16(h->destinationPort) ||
		ips->remoteAddress.value != packet->source.value
	){
		return 0;
	}
	return 1;
}

static void deleteTCPRawSocket(IPSocket *ips){
	TCPRawSocket *tcps = ips->instance;
	DELETE(tcps);
}

static int openTCPRaw(OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength, OpenFileMode ofm){
	TCPRawSocket *NEW(tcps);
	initIPSocket(&tcps->ipSocket, tcps, transmitIPV4Packet, filterTCPPacket, receiveIPV4Packet, deleteTCPRawSocket);
	int ok = scanIPSocketArguments(&tcps->ipSocket, fileName, nameLength);
	if(!ok || ofm.writable == 0){
		return 0;
	}
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.getParameter= getTCPRawParameter;
	ff.read = readTCPRawSocket;
	ff.write = writeTCPRawSocket;
	ff.close = closeTCPRawSocket;
	completeOpenFile(ofr, tcps, &ff);
	return 1;
}

void initTCP(void){
	int ok;
	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openTCPRaw;
	ok = addFileSystem(&fnf, "tcpraw", strlen("tcpraw"));
	if(!ok){
		panic("cannot initialize TCP raw file system");
	}
	fnf.open = openTCPClient;
	ok = addFileSystem(&fnf, "tcpclient", strlen("tcpclient"));
	if(!ok){
		panic("cannot initialize TCP client file system");
	}
	//fnf.open = openTCPServer;
	//addFileSystem(&fnf, "tcpserver", strlen("tcpserver"));
}
