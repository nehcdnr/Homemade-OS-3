#include"common.h"
#include"kernel.h"
#include"resource/resource.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"io/fifo.h"
#include"network.h"

#pragma pack(1)

typedef union{
	struct{
		uint8_t ns: 1; // nonce sum
		uint8_t reserved: 3;
		uint8_t dataOffset: 4;
		uint8_t fin: 1; // finish
		uint8_t syn: 1; // synchronize sequence number
		uint8_t rst: 1; // reset
		uint8_t psh: 1; // push
		uint8_t ack: 1;
		uint8_t urg: 1; // urgent pointer effective
		uint8_t ece: 1; // ECN echo
		uint8_t cwr: 1; // congestion window reduced
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
/*
static uint8_t *getTCPData(const TCPHeader *h){
	return ((uint8_t*)h) + getTCPHeaderSize(h);
}

static uintptr_t getTCPDataSize(IPV4Header *ip, TCPHeader *tcp){
	return getIPDataSize(ip) - getTCPHeaderSize(tcp);
}
*/
static uint32_t diffTCPSequence(uint32_t a, uint32_t b){
	return (a - b) & 0xffffffff;
}

static uint32_t addTCPSequence(uint32_t a, uint32_t b){
	return (a + b) & 0xffffffff;
}

typedef struct{
	IPV4Header ip;
	TCPHeader tcp;
}TCPIPHeader;

enum TCPOptionCode{
	TCP_OPTION_END = 0,
	TCP_OPTION_NOP = 1,
	TCP_OPTION_MAX_SEGMENT_SIZE = 2,
	TCP_OPTION_WINDOW_SCALE = 3,
	TCP_OPTION_SACK_PERMITTED = 4,
	TCP_OPTION_SACK = 5,
	TCP_OPTION_TIMESTAMP = 8
};

// if data == NULL then initialize the header but do not copy the data
// flags.dataOffset and flags.reserved are ignored
static uintptr_t initTCPPacket(
	TCPHeader *tcp,	uint32_t seqNumber, uint32_t ackNumber, uint16_t windowSize, TCPFlags flags,
	/*IPV4Address localAddr, */uint16_t localPort,
	/*IPV4Address remoteAddr, */uint16_t remotePort
){
	// do not clear packet here
	// memset(tcp, 0, sizeof(TCPHeader));
	tcp->sourcePort = changeEndian16(localPort);
	tcp->destinationPort = changeEndian16(remotePort);
	tcp->sequenceNumber = changeEndian32(seqNumber);
	tcp->acknowledgeNumber = changeEndian32(ackNumber);
	tcp->flags = flags;
	tcp->flags.reserved = 0;
	// tcp->flags.dataOffset = sizeof(TCPHeader) / sizeof(uint32_t);
	tcp->windowsSize = changeEndian16(windowSize);
	tcp->checksum = 0;
	tcp->urgentPointer = 0;
	return sizeof(*tcp);
}

static uint32_t getAcknowledgeNumber(const TCPHeader *tcp){
	return changeEndian32(tcp->acknowledgeNumber);
}

static uintptr_t initTCPData(TCPHeader *tcp, uintptr_t offset){
	assert(offset % sizeof(uint32_t) == 0);
	tcp->flags.dataOffset = offset / sizeof(uint32_t);
	return offset;
}

static uintptr_t finishInitTCPPacket(
	TCPHeader *tcp, uintptr_t offset, IPV4Address localAddr, IPV4Address remoteAddr
){
	tcp->checksum = 0;
	tcp->checksum = calculateIPDataChecksum2(tcp, offset, localAddr, remoteAddr, IP_DATA_PROTOCOL_TCP);
	assert(calculateIPDataChecksum2(tcp, offset, localAddr, remoteAddr, IP_DATA_PROTOCOL_TCP) == 0);
	return offset;
}

#define APPEND_OPTION(P, I, T, V) do{\
	*(T*)(((uintptr_t)(P)) + (I)) = (V);\
	(I) += sizeof(T);\
}while(0)

static uintptr_t appendTCPMaxSegmentSize(TCPHeader *tcp, uintptr_t offset, uint16_t ssize){
	uintptr_t o = offset;
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_MAX_SEGMENT_SIZE);
	APPEND_OPTION(tcp, o, uint8_t, 4);
	APPEND_OPTION(tcp, o, uint16_t, changeEndian16(ssize));
	assert(offset + 4 == o);
	return o;
}

static uintptr_t appendTCPWindowScale(TCPHeader *tcp, uintptr_t offset, uint8_t scale){
	uintptr_t o = offset;
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_WINDOW_SCALE);
	APPEND_OPTION(tcp, o, uint8_t, 3);
	APPEND_OPTION(tcp, o, uint8_t, scale);
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_NOP);
	assert(offset + 4 == o);
	return o;
}
/*
static uintptr_t appendTCPSACKPermitted(TCPHeader *tcp, uintptr_t offset){
	uintptr_t o = offset;
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_SACK_PERMITTED);
	APPEND_OPTION(tcp, o, uint8_t, 2);
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_NOP);
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_NOP);
	assert(offset + 4 == o);
	return o;
}
*/
static uintptr_t appendTCPOptionEnd(TCPHeader *tcp, uintptr_t offset){
	uintptr_t o = offset;
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_END);
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_END);
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_END);
	APPEND_OPTION(tcp, o, uint8_t, TCP_OPTION_END);
	assert(offset + 4 == o);
	return o;
}

#undef APPEND_OPTION

#define PARSE_OPTION_CODE(P, O) do{\
	if((P)[0] != (O)){\
		return 0;\
	}\
}while(0)

#define PARSE_OPTION_SIZE(P, S) do{\
	if((P)[1] != 2 + (S)){\
		return 0;\
	}\
}while(0)

#define PARSE_OPTION_DATA(P, T, V) do{\
	PARSE_OPTION_SIZE(P, sizeof(T));\
	(V) = (*(T*)((P) + 2));\
}while(0)

static int parseTCPOptionEnd(uint8_t *p){
	PARSE_OPTION_CODE(p, TCP_OPTION_END);
	return 1;
}

static int parseTCPOptionNOP(uint8_t *p){
	PARSE_OPTION_CODE(p, TCP_OPTION_NOP);
	return 1;
}

static int parseTCPMaxSegmentSize(uint8_t *p, uintptr_t *maxSegSize){
	PARSE_OPTION_CODE(p, TCP_OPTION_MAX_SEGMENT_SIZE);
	PARSE_OPTION_DATA(p, uint16_t, *maxSegSize);
	*maxSegSize = changeEndian16(*maxSegSize);
	return 1;
}
static int parseTCPWindowScale(uint8_t *p, uintptr_t *windowScale){
	PARSE_OPTION_CODE(p, TCP_OPTION_WINDOW_SCALE);
	PARSE_OPTION_DATA(p, uint8_t, *windowScale);
	return 1;
}
static int parseTCPSACKPermitted(uint8_t *p, int *sackPermitted){
	PARSE_OPTION_CODE(p, TCP_OPTION_SACK_PERMITTED);
	PARSE_OPTION_SIZE(p, 0);
	*sackPermitted = 1;
	return 1;
}

#undef PARSE_OPTION_CODE
#undef PARSE_OPTION_SIZE
#undef PARSE_OPTION_DATA

#define DEFAULT_TCP_MAX_SEGMENT_SIZE (1452)

static int parseTCPOptions(
	const TCPHeader *tcp,
	uintptr_t *maxSegSize, uintptr_t *windowScale, int *sackPermitted
){
	*maxSegSize = DEFAULT_TCP_MAX_SEGMENT_SIZE;
	*windowScale = 0;
	*sackPermitted = 0;
	const uintptr_t headerSize = getTCPHeaderSize(tcp);
	uintptr_t offset = sizeof(*tcp);
	uint8_t *const p = ((uint8_t*)tcp);
	while(offset != headerSize){
		if(parseTCPOptionEnd(p)){
			break;
		}
		if(parseTCPOptionNOP(p)){
			offset++;
			continue;
		}
		// check if option size exceeds header
		if(offset + 1 >= headerSize){
			return 0;
		}
		uintptr_t optionSize = p[offset + 1];
		if(optionSize < 2 || offset + optionSize > headerSize){
			return 0;
		}
		parseTCPMaxSegmentSize(p + offset, maxSegSize);
		parseTCPWindowScale(p + offset, windowScale);
		parseTCPSACKPermitted(p + offset, sackPermitted);
		offset += optionSize;
	}
	return 1;
}

typedef struct{
	uint32_t sequenceBegin;
	uintptr_t sequenceLength;
	uintptr_t windowSize;
	uint8_t *buffer;
}TCPReceiveWindow;

static int initTCPReceiveWindow(TCPReceiveWindow *rw, uintptr_t windowSize){
	rw->sequenceBegin = 0;
	rw->sequenceLength = 0;
	rw->windowSize = windowSize;
	rw->buffer = allocateKernelMemory(windowSize);
	if(rw->buffer == NULL){
		return 0;
	}
	return 1;
}

static void synReceiveWindow(TCPReceiveWindow *rw, uint32_t seq){
	rw->sequenceBegin = seq;
	rw->sequenceLength = 0;
}

static void copyTCPReceiveWindow(TCPReceiveWindow *rw, uint32_t remoteSeqBegin, const uint8_t *buffer, uintptr_t bufferSize){
	uintptr_t i;
	for(i = 0; i < bufferSize; i++){
		uint32_t s = ((remoteSeqBegin + i) & 0xffffffff);
		if(diffTCPSequence(s, rw->sequenceBegin) >= rw->windowSize){
			printk("warning: packet not in window\n");
			continue;
		}
		// copy
		// assert(rw->windowSize > 0);
		rw->buffer[s % rw->windowSize] = buffer[i];
		// extend buffer
		uint32_t sequenceEnd = ((rw->sequenceBegin + rw->sequenceLength) & 0xffffffff);
		if(s == sequenceEnd && rw->sequenceLength < rw->windowSize){
			rw->sequenceLength++;
		}
	}
}

static void destroyTCPReceiveWindow(TCPReceiveWindow *rw){
	releaseKernelMemory(rw->buffer);
}

typedef struct TCPTransmitBuffer{
	uintptr_t size;
	uintptr_t offset;
	struct TCPTransmitBuffer *next;

	uint8_t buffer[];
}TCPTransmitBuffer;
/*
static TCPTransmitBuffer *createTCPTransmitBuffer(const uint8_t *buffer, uintptr_t size){
	TCPTransmitBuffer *tb = allocateKernelMemory(sizeof(TCPTransmitBuffer) + size);
	if(tb == NULL){
		return NULL;
	}
	tb->size = size;
	tb->offset = 0;
	tb->next = NULL;
	memcpy(tb->buffer, buffer, size);
	return tb;
}
*/
static void deleteTCPTransmitBuffer(TCPTransmitBuffer *tb){
	releaseKernelMemory(tb);
}

typedef struct{
	uint32_t sequenceBegin;
	uint32_t windowSize;
	TCPTransmitBuffer *head, *tail;
}TCPTransmitWindow;

static void initTCPTransmitWindow(TCPTransmitWindow *tw, uint32_t seqBegin){
	tw->sequenceBegin = seqBegin;
	tw->windowSize = 0;
	tw->head = NULL;
	tw->tail = NULL;
}

static void setTransmitWindow(TCPTransmitWindow *tw, uint16_t windowSize){
	tw->windowSize = windowSize;
}
/*
static int pushTCPTransmitBuffer(TCPTransmitWindow *tw, const uint8_t *buffer, uintptr_t size){
	TCPTransmitBuffer *tb = createTCPTransmitBuffer(buffer, size);
	if(tb == NULL){
		return 0;
	}
	// push
	if(tw->tail == NULL){
		tw->head = tb;
	}
	else{
		tw->tail->next = tb;
	}
	tw->tail = tb;
	tb->next = NULL;
	return 1;
}
*/
static TCPTransmitBuffer *popTCPTransmitBuffer(TCPTransmitWindow *tw){
	assert(tw->tail != NULL);
	TCPTransmitBuffer *tb = tw->head;
	tw->head = tb->next;
	if(tw->head == NULL){
		tw->tail = NULL;
	}
	return tb;
}

//  from file request to packet
static uintptr_t copyTCPTransmitBuffer(const TCPTransmitWindow *tw, uint8_t *buffer, uintptr_t bufferSize){
	TCPTransmitBuffer *tb = tw->head;
	uintptr_t offset = 0;
	while(bufferSize - offset > 0){
		if(tb == NULL){
			break;
		}
		const uintptr_t copySize = MIN(bufferSize - offset, tb->size - tb->offset);
		memcpy(buffer + offset, tb->buffer + tb->offset, copySize);
		offset += copySize;
	}
	return offset;
}

static void ackTransmitWindow(TCPTransmitWindow *tw, uint32_t ack){
	uint32_t receiveSize = ack - tw->sequenceBegin;
	while(tw->tail != NULL && receiveSize != 0){
		TCPTransmitBuffer *tb = popTCPTransmitBuffer(tw);
		uintptr_t ackSize = MIN(tb->size - tb->offset, receiveSize);
		tb->offset += ackSize;
		if(tb->offset == tb->size){
			deleteTCPTransmitBuffer(tb);
		}
		receiveSize -= ackSize;
	}
	tw->sequenceBegin = ack;
}
/*
static void destroyTCPTransmitWindow(TCPTransmitWindow *tw){
	while(tw->tail != NULL){
		TCPTransmitBuffer *tb = popTCPTransmitBuffer(tw);
		deleteTCPTransmitBuffer(tb);
	}
}
*/
typedef struct{
	TCPReceiveWindow receiveWindow;
	TCPTransmitWindow transmitWindow;
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

static int transmitTCPPacket(const TCPSocket *tcps, int syn, int ack){
	// header
	TCPFlags flags;
	flags.value = 0;
	if(syn){
		flags.syn = 1;
	}
	if(ack){
		flags.ack = 1;
	}
	uint32_t ackNumber = addTCPSequence(tcps->receiveWindow.sequenceBegin, tcps->receiveWindow.sequenceLength);
	uint16_t remainWindowSize = (tcps->receiveWindow.windowSize - tcps->receiveWindow.sequenceLength);
	uintptr_t offset = initTCPPacket(
		tcps->rawBuffer,
		tcps->transmitWindow.sequenceBegin, ackNumber, remainWindowSize, flags,
		tcps->localPort, tcps->remotePort
	);
	if(syn){
		offset = appendTCPWindowScale(tcps->rawBuffer, offset, 0);
		// 1460 for Ethernet/IP/TCP
		offset = appendTCPMaxSegmentSize(tcps->rawBuffer, offset, tcps->rawBufferSize - sizeof(TCPHeader));
		//offset = appendTCPSACKPermitted(tcps->rawBuffer, offset);
		offset = appendTCPOptionEnd(tcps->rawBuffer, offset);
	}
	// data
	offset = initTCPData(tcps->rawBuffer, offset);
	offset += copyTCPTransmitBuffer(&tcps->transmitWindow, ((uint8_t*)tcps->rawBuffer) + offset, tcps->rawBufferSize - offset);
	uintptr_t packetSize = finishInitTCPPacket(tcps->rawBuffer, offset, tcps->localAddress, tcps->remoteAddress);
	uintptr_t rwSize = packetSize;
	// write
	uintptr_t r = syncWriteFile(tcps->rawSocketHandle, tcps->rawBuffer, &rwSize);
	if(r == IO_REQUEST_FAILURE || rwSize != packetSize){
		return 0;
	}
	return 1;
}

static int transmitTCPSYNPacket(const TCPSocket *tcps){
	return transmitTCPPacket(tcps, 1, 0);
}

static int transmitTCPDataPacket(const TCPSocket *tcps){
	return transmitTCPPacket(tcps, 0, 1);
}

static int receiveTCPPacket(
	TCPHeader *rawBuffer, uintptr_t readSize,
	TCPReceiveWindow *receiveWindow, TCPTransmitWindow *transmitWindow
){
	uintptr_t mss, ws;
	int sack;
	int ok = parseTCPOptions(rawBuffer, &mss, &ws, &sack);
	if(!ok){
		//printk("TCP option error\n");
		return 0;
	}
	if(rawBuffer->flags.syn){
		if(getAcknowledgeNumber(rawBuffer) != transmitWindow->sequenceBegin + 1){
			//printk("TCP SYN ACK number error %x %x\n", rawBuffer->flags.value, rawBuffer->acknowledgeNumber);
			return 0;
		}
	}
	// modify local state
	uint32_t remoteSeqNumber = changeEndian32(rawBuffer->sequenceNumber);
	uint16_t remoteWindowSize = changeEndian16(rawBuffer->windowsSize);
	if(rawBuffer->flags.ack){
		ackTransmitWindow(transmitWindow, getAcknowledgeNumber(rawBuffer));
	}
	setTransmitWindow(transmitWindow, remoteWindowSize);
	if(rawBuffer->flags.syn){
		synReceiveWindow(receiveWindow, remoteSeqNumber + 1);
	}
	else{
		uintptr_t dataBegin = getTCPHeaderSize(rawBuffer);
		copyTCPReceiveWindow(receiveWindow, remoteSeqNumber, ((uint8_t*)rawBuffer) + dataBegin, readSize - dataBegin);
	}
	return 1;
}

static int receiveTCPSYNACK(TCPSocket *tcps){
	uintptr_t rwSize = tcps->rawBufferSize;
	uintptr_t r = syncReadFile(tcps->rawSocketHandle, tcps->rawBuffer, &rwSize);
	// check header size and checksum in validateTCPPacket()
	if(r == IO_REQUEST_FAILURE || rwSize < getTCPHeaderSize(tcps->rawBuffer)){
		printk("TCP handshake 2 size error\n");
		return 0;
	}
	if(tcps->rawBuffer->flags.syn == 0 || tcps->rawBuffer->flags.ack == 0){
		printk("TCP handshake 2 flags error\n", tcps->rawBuffer->flags.value);
		return 0;
	}
	return receiveTCPPacket(tcps->rawBuffer, rwSize, &tcps->receiveWindow, &tcps->transmitWindow);
}

static int tcpHandShake(TCPSocket *tcps){
	// 1. SYN
	if(transmitTCPSYNPacket(tcps) == 0){
		printk("TCP handshake 1 error\n");
		return 0;
	}
	// 2. SYN ACK
	if(receiveTCPSYNACK(tcps) == 0){
		return 0;
	}// TODO: retransmission
	// 3. ACK
	if(transmitTCPDataPacket(tcps) == 0){
		printk("TCP handshake 3 error\n");
		return 0;
	}
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
			printk("readSocketIO %d\n", readSize);
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

#define DEFAULT_TCP_RECEIVE_WINDOW_SIZE (8192)

static TCPSocket *createTCPSocket(const char *fileName, uintptr_t nameLength){
	const char *tcpraw = "tcpraw:";
	const uintptr_t tcpRawNameLength = nameLength + strlen(tcpraw);
	char *tcpRawName = allocateKernelMemory(sizeof(char) * tcpRawNameLength);
	EXPECT(tcpRawName != NULL);
	snprintf(tcpRawName, tcpRawNameLength, "%s%s", tcpraw, fileName);

	TCPSocket *NEW(tcps);
	EXPECT(tcps != NULL);
	initTCPTransmitWindow(&tcps->transmitWindow, 9999);
	int ok = initTCPReceiveWindow(&tcps->receiveWindow, DEFAULT_TCP_RECEIVE_WINDOW_SIZE);
	EXPECT(ok);
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
	destroyTCPReceiveWindow(&tcps->receiveWindow);
	// destroyTCPTransmitWindow(&tcps->transmitWindow);
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
	int ok = tcpHandShake(tcps);
	EXPECT(ok);

	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	//ff.read = readTCP;
	//ff.write = writeTCP;
	ff.close = closeTCP;
	completeOpenFile(arg->ofr, tcps, &ff);

	while(tcpLoop(tcps)){
	}

	deleteTCPSocket(tcps);
	systemCall_terminate();

	ON_ERROR;
	//TODO: deleteTCPSocket
	ON_ERROR;
	failOpenFile(arg->ofr);
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
	initIPSocket(
		&tcps->ipSocket, tcps, IP_DATA_PROTOCOL_TCP,
		transmitIPV4Packet, filterTCPPacket, receiveIPV4Packet, deleteTCPRawSocket
	);
	int ok = scanIPSocketArguments(&tcps->ipSocket, fileName, nameLength);
	EXPECT(ok && ofm.writable);
	ok = startIPSocketTasks(&tcps->ipSocket);
	EXPECT(ok);
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.getParameter= getTCPRawParameter;
	ff.read = readTCPRawSocket;
	ff.write = writeTCPRawSocket;
	ff.close = closeTCPRawSocket;
	completeOpenFile(ofr, tcps, &ff);
	return 1;
	stopIPSocketTasks(&tcps->ipSocket);
	ON_ERROR;
	// invalid argument
	ON_ERROR;
	return 0;
}

void initTCP(void){
	int ok;
	// see createTCPSocket
	ok = waitForFirstResource("fifo", RESOURCE_FILE_SYSTEM, matchName);
	if(!ok){
		panic("cannot initialize TCP FIFO");
	}
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

#ifndef NDEBUG

void testTCP(void);
void testTCP(void){
	// wait for DHCP
	sleep(5000);
	printk("test TCP client\n");
	int ok = waitForFirstResource("tcpclient", RESOURCE_FILE_SYSTEM, matchName);
	assert(ok);
	const char *target = "tcpclient:192.168.56.1:59999;srcport=59998";
	uintptr_t tcp = syncOpenFileN(target, strlen(target), OPEN_FILE_MODE_WRITABLE);
	assert(tcp != IO_REQUEST_FAILURE);
	while(1){
		sleep(1);
	}
	uintptr_t r;
	r = syncCloseFile(tcp);
	assert(r != IO_REQUEST_FAILURE);
}

#endif
