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
static uintptr_t diffTCPSequence(uint32_t a, uint32_t b){
	return (a - b) & 0xffffffff;
}

static uint32_t addTCPSequence(uint32_t a, uintptr_t b){
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

#define DEFAULT_TCP_MAX_SEGMENT_SIZE (576)
#define MAX_TCP_WINDOW_SCALE (14)
#define DEFAULT_TCP_WINDOW_SCALE (0)

// if option is not present, do not modify its value
static int parseTCPOptions(
	const TCPHeader *tcp,
	uintptr_t *maxSegSize, uintptr_t *windowScale, int *sackPermitted
){
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

typedef struct TCPReceiveBuffer{
	RWFileRequest *rwfr;
	uint8_t *buffer;
	uintptr_t bufferSize;

	struct TCPReceiveBuffer **prev, *next;
}TCPReceiveBuffer;

static TCPReceiveBuffer *createTCPReceiveBuffer(RWFileRequest *rwfr, uint8_t *buffer, uintptr_t bufferSize){
	TCPReceiveBuffer *NEW(rb);
	if(rb == NULL){
		return NULL;
	}
	rb->rwfr = rwfr;
	rb->buffer = buffer;
	rb->bufferSize = bufferSize;
	rb->prev = NULL;
	rb->next = NULL;
	return rb;
}

static void deleteTCPReceiveBuffer(TCPReceiveBuffer *rb){
	DELETE(rb);
}

typedef struct{
	uint32_t sequenceBegin;
	uintptr_t sequenceLength;
	// IMPROVE: separate bufferSize and windowSize; currently bufferSize is windowSize
	// uintptr_t bufferBegin;
	// uintptr_t bufferSize;
	uintptr_t windowSize;
	struct{
		uint8_t hasData;
		uint8_t data;
	}*buffer;

	TCPReceiveBuffer tail[1];
	TCPReceiveBuffer *head;
}TCPReceiveWindow;

static int initTCPReceiveWindow(TCPReceiveWindow *rw, uintptr_t windowSize){
	rw->sequenceBegin = 0;
	rw->sequenceLength = 0;
	rw->windowSize = windowSize;
	NEW_ARRAY(rw->buffer, windowSize);
	EXPECT(rw->buffer != NULL);
	memset(rw->buffer, 0, windowSize * (sizeof(*rw->buffer)));

	MEMSET0(rw->tail);
	rw->tail->next = NULL;
	rw->tail->prev = NULL;
	rw->head = NULL;
	ADD_TO_DQUEUE(rw->tail, &rw->head);
	return 1;
	DELETE(rw->buffer);
	ON_ERROR;
	return 0;
}

static void synReceiveWindow(TCPReceiveWindow *rw, uint32_t seq){
	rw->sequenceBegin = seq;
	rw->sequenceLength = 0;
}

static void setTCPReceiveData(TCPReceiveWindow *rw, uintptr_t seq, uint8_t data){
	const uintptr_t i = seq % rw->windowSize;
	rw->buffer[i].hasData = 1;
	rw->buffer[i].data = data;
}

static int hasTCPReceiveData(TCPReceiveWindow *rw, uintptr_t seq){
	return (int)rw->buffer[seq % rw->windowSize].hasData;
}

static uint8_t unsetTCPReceiveData(TCPReceiveWindow *rw, uintptr_t seq){
	const uintptr_t i = seq % rw->windowSize;
	assert(rw->buffer[i].hasData);
	uint8_t d = rw->buffer[i].data;;
	rw->buffer[i].data = 0;
	rw->buffer[i].hasData = 0;
	return d;
}

static uintptr_t copyTCPReceiveWindow(
	TCPReceiveWindow *rw, uint32_t remoteSeqBegin,
	const uint8_t *buffer, uintptr_t bufferSize
){
	uintptr_t i;
	for(i = 0; i < bufferSize; i++){
		uint32_t s = addTCPSequence(remoteSeqBegin, i);
		if(diffTCPSequence(s, rw->sequenceBegin) >= rw->windowSize){
			printk("warning: sequence %u not in window (seq %u, len %u)\n", s, rw->sequenceBegin, rw->windowSize);
			continue;
		}
		// copy
		// assert(rw->windowSize > 0);
		setTCPReceiveData(rw, s, buffer[i]);
	}
	// extend buffer
	uintptr_t oldSeqLen = rw->sequenceLength;
	while(rw->sequenceLength < rw->windowSize){
		uint32_t sequenceEnd = addTCPSequence(rw->sequenceBegin, rw->sequenceLength);
		if(hasTCPReceiveData(rw, sequenceEnd) == 0)
			break;
		rw->sequenceLength++;
	}
	return rw->sequenceLength - oldSeqLen;
}

static void pushTCPReceiveBuffer(TCPReceiveWindow *rw, RWFileRequest *rwfr, uint8_t *buffer, uintptr_t bufferSize){
	TCPReceiveBuffer *rb = createTCPReceiveBuffer(rwfr, buffer, bufferSize);
	if(rb == NULL){
		completeRWFileIO(rwfr, 0 ,0);
		return;
	}
	TCPReceiveBuffer **tailPrev = rw->tail->prev;
	ADD_TO_DQUEUE(rb, tailPrev);
}

static void popTCPReceiveBuffer(TCPReceiveWindow *rw, uintptr_t receiveSize){
	assert(rw->head != rw->tail);
	TCPReceiveBuffer *rb = rw->head;
	REMOVE_FROM_DQUEUE(rb);
	completeRWFileIO(rb->rwfr, receiveSize, 0);
	deleteTCPReceiveBuffer(rb);
}

static int hasTCPReceiveBuffer(TCPReceiveWindow *rw){
	return rw->head != rw->tail;
}

static void copyTCPReceiveBuffer(TCPReceiveWindow *rw, int partialRead){
	while(rw->sequenceLength != 0 && hasTCPReceiveBuffer(rw)){
		TCPReceiveBuffer *rb = rw->head;
		if(partialRead == 0 && rw->sequenceLength < rb->bufferSize){
			break;
		}
		uintptr_t bi = 0, copySize = MIN(rb->bufferSize, rw->sequenceLength);
		uint32_t wi = rw->sequenceBegin;
		while(bi < copySize){
			rb->buffer[bi] = unsetTCPReceiveData(rw, wi);
			wi = addTCPSequence(wi, 1);
			bi++;
		}
		rw->sequenceBegin = wi;
		rw->sequenceLength -= copySize;
		popTCPReceiveBuffer(rw, copySize);
	}
}

static void destroyTCPReceiveWindow(TCPReceiveWindow *rw){
	// fail all receive request
	while(hasTCPReceiveBuffer(rw)){
		popTCPReceiveBuffer(rw, 0);
	}
	DELETE(rw->buffer);
}

typedef struct TCPTransmitBuffer{
	uintptr_t size;
	uint32_t sequenceBegin;
	struct TCPTransmitBuffer *next, **prev;

	uint8_t buffer[];
}TCPTransmitBuffer;

static TCPTransmitBuffer *createTCPTransmitBuffer(const uint8_t *buffer, uint32_t seqNumber, uintptr_t size){
	TCPTransmitBuffer *tb = allocateKernelMemory(sizeof(TCPTransmitBuffer) + size);
	if(tb == NULL){
		return NULL;
	}
	tb->sequenceBegin = seqNumber;
	tb->size = size;
	tb->next = NULL;
	tb->prev = NULL;
	memcpy(tb->buffer, buffer, size);
	return tb;
}

static void deleteTCPTransmitBuffer(TCPTransmitBuffer *tb){
	releaseKernelMemory(tb);
}

typedef struct{
	uint32_t sequenceBegin;
	uintptr_t windowSize;
	uintptr_t lastWindowScale;
	uintptr_t maxSegmentSize;

	TCPTransmitBuffer tail[1];
	TCPTransmitBuffer *head;
	TCPTransmitBuffer *current;
	// current->sequenceNumber <= currentSequence <= current->sequenceNumber + current->size
	uint32_t currentSequence;
}TCPTransmitWindow;

static void initTCPTransmitWindow(TCPTransmitWindow *tw, uint32_t seqBegin){
	tw->sequenceBegin = seqBegin;
	tw->windowSize = 0;
	tw->lastWindowScale = DEFAULT_TCP_WINDOW_SCALE;
	tw->maxSegmentSize = DEFAULT_TCP_MAX_SEGMENT_SIZE;
	tw->tail->sequenceBegin = seqBegin;
	tw->tail->size = 0;
	tw->tail->next = NULL;
	tw->tail->prev = NULL;
	tw->head = NULL;
	ADD_TO_DQUEUE(tw->tail, &tw->head);
	tw->current = tw->head;
	tw->currentSequence = tw->head->sequenceBegin;
}

static void setTransmitWindow(TCPTransmitWindow *tw, uint16_t windowSize, uintptr_t windowScale){
	windowScale = MIN(DEFAULT_TCP_WINDOW_SCALE, windowScale);
	tw->windowSize = (((uintptr_t)windowSize) << windowScale);
	tw->lastWindowScale = windowScale;
}

static void setTransmitSegmentSize(TCPTransmitWindow *tw, uintptr_t mss){
	tw->maxSegmentSize = mss;
}

static int pushTCPTransmitBuffer(TCPTransmitWindow *tw, const uint8_t *buffer, uintptr_t size){
	TCPTransmitBuffer *tb = createTCPTransmitBuffer(buffer, tw->tail->sequenceBegin, size);
	if(tb == NULL){
		return 0;
	}
	// push
	TCPTransmitBuffer **const tailPrev = tw->tail->prev;
	ADD_TO_DQUEUE(tb, tailPrev);
	tw->tail->sequenceBegin = addTCPSequence(tb->sequenceBegin, size);
	if(tw->current == tw->tail){
		tw->current = tb;
		assert(tw->currentSequence == tb->sequenceBegin);
	}
	return 1;
}

static void popTCPTransmitBuffer(TCPTransmitWindow *tw){
	assert(tw->head != tw->tail);
	TCPTransmitBuffer *tb = tw->head;
	if(tw->current == tb){
		tw->current = tb->next;
		tw->currentSequence = tw->current->sequenceBegin;
	}
	REMOVE_FROM_DQUEUE(tb);
	deleteTCPTransmitBuffer(tb);
}

static int hasTCPTransmitBuffer(TCPTransmitWindow *tw){
	return tw->head != tw->tail;
}

//  from file request to packet
static uintptr_t copyTCPTransmitBuffer(TCPTransmitWindow *tw, uint8_t *buffer, uintptr_t bufferSize){
	uintptr_t offset = 0;
	uintptr_t maxCopySize = MIN(tw->windowSize, bufferSize);
	maxCopySize = MIN(maxCopySize, tw->maxSegmentSize);
	while(maxCopySize > offset && tw->current != tw->tail){
		TCPTransmitBuffer *const c = tw->current;
		const uintptr_t currentOffset = diffTCPSequence(tw->currentSequence, c->sequenceBegin);
		const uintptr_t copySize = MIN(maxCopySize - offset, c->size - currentOffset);
		memcpy(buffer + offset, c->buffer + currentOffset, copySize);
		offset += copySize;
		tw->currentSequence = addTCPSequence(tw->currentSequence, copySize);
		if(c->size == diffTCPSequence(tw->currentSequence, c->sequenceBegin)){
			tw->current = c->next;
			assert(tw->current->sequenceBegin == tw->currentSequence);
		}
	}
	return offset;
}

static uintptr_t ackTransmitWindow(TCPTransmitWindow *tw, uint32_t ack){
	const uintptr_t totalACKSize = diffTCPSequence(ack, tw->sequenceBegin);
	uintptr_t ackDiff = totalACKSize;
	while(hasTCPTransmitBuffer(tw) && ackDiff != 0){
		uintptr_t headOffset = diffTCPSequence(tw->sequenceBegin, tw->head->sequenceBegin);
		uintptr_t ackSize = MIN(tw->head->size - headOffset, ackDiff);
		tw->sequenceBegin = addTCPSequence(tw->sequenceBegin, ackSize);
		if(tw->head->size == diffTCPSequence(tw->sequenceBegin, tw->head->sequenceBegin)){
			popTCPTransmitBuffer(tw);
		}
		ackDiff -= ackSize;
	}
	if(ackDiff != 0){ // in handshake 2, ack == sequenceBegin + 1
		assert(tw->current == tw->tail && tw->head == tw->tail);
		tw->sequenceBegin = tw->tail->sequenceBegin = tw->currentSequence = ack;
		//printk("warning: unexpected ack=%u; seq=%u; winLen=%u\n", ack, tw->sequenceBegin, tw->windowSize);
	}
	assert(tw->sequenceBegin == ack);
	return totalACKSize;
}

static void destroyTCPTransmitWindow(TCPTransmitWindow *tw){
	while(hasTCPTransmitBuffer(tw)){
		popTCPTransmitBuffer(tw);
	}
}

typedef struct{
	TCPReceiveWindow receiveWindow;
	TCPTransmitWindow transmitWindow;
	// IP packet from TCP thread to IP transmit thread
	uintptr_t rawSocketHandle;
	// RWTCPRequest from user thread to TCP thread
	uintptr_t rwRequestFIFOHandle;
	// read/write buffer for rawSocketHandle
	TCPHeader *receiveBuffer;
	TCPHeader *transmitBuffer;
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

static int writeRWTCPRequest(uintptr_t fifoHandle, RWFileRequest *rwfr, uint8_t *buffer, uintptr_t bufferSize, int isWrite){
	RWTCPRequest r;
	r.rwfr = rwfr;
	r.buffer = buffer;
	r.bufferSize = bufferSize;
	r.isWrite = isWrite;
	uintptr_t writeSize = sizeof(r);
	uintptr_t w = syncWriteFile(fifoHandle, &r, &writeSize);
	if(w == IO_REQUEST_FAILURE || writeSize != sizeof(r)){
		return 0;
	}
	return 1;
}

// if mustTransmit != 0, transmit an ACK even if no data
static int transmitTCPDataPacket(TCPSocket *tcps, int mustTransmit, uintptr_t *transmitSize){
	// header
	TCPFlags flags;
	flags.value = 0;
	flags.ack = 1;
	TCPTransmitWindow *const tw = &tcps->transmitWindow;
	TCPReceiveWindow *const rw = &tcps->receiveWindow;
	TCPHeader *const tb = tcps->transmitBuffer;
	uint32_t localACKNumber = addTCPSequence(rw->sequenceBegin, rw->sequenceLength);
	uint16_t localWindowRemain = (rw->windowSize - rw->sequenceLength);
	*transmitSize = 0;
	while(mustTransmit || tw->current != tw->tail){
		uintptr_t offset = initTCPPacket(
				tb,
				tw->currentSequence, localACKNumber, localWindowRemain, flags,
				tcps->localPort, tcps->remotePort
		);
		// offset = appendTCPWindowScale(tb, offset, 0);
		// data
		offset = initTCPData(tb, offset);
		uintptr_t copySize = copyTCPTransmitBuffer(tw, ((uint8_t*)tb) + offset, tcps->rawBufferSize - offset);
		if(mustTransmit == 0 && copySize == 0){
			break;
		}
		offset += copySize;
		uintptr_t packetSize = finishInitTCPPacket(tb, offset, tcps->localAddress, tcps->remoteAddress);
		uintptr_t rwSize = packetSize;
		// write
		uintptr_t r = syncWriteFile(tcps->rawSocketHandle, tb, &rwSize);
		if(r == IO_REQUEST_FAILURE || rwSize != packetSize){
			return 0;
		}
		*transmitSize += copySize;
		mustTransmit = 0;
	}
	return 1;
}

static int transmitTCPSYNPacket(TCPSocket *tcps, int ack){
	// header
	TCPFlags flags;
	flags.value = 0;
	flags.syn = 1;
	if(ack){
		flags.ack = 1;
	}
	TCPTransmitWindow *const tw = &tcps->transmitWindow;
	TCPReceiveWindow *const rw = &tcps->receiveWindow;
	TCPHeader *const tb = tcps->transmitBuffer;
	uint32_t localACKNumber = addTCPSequence(rw->sequenceBegin, rw->sequenceLength);
	uint16_t localWindowRemain = (rw->windowSize - rw->sequenceLength);
	uintptr_t offset = initTCPPacket(
		tb,
		tw->currentSequence, localACKNumber, localWindowRemain, flags,
		tcps->localPort, tcps->remotePort
	);
	offset = appendTCPWindowScale(tb, offset, 0);
	// 1460 for Ethernet/IP/TCP
	offset = appendTCPMaxSegmentSize(tb, offset, tcps->rawBufferSize - sizeof(TCPHeader)/* - size of options*/);
	//offset = appendTCPSACKPermitted(tcps->rawBuffer, offset);
	offset = appendTCPOptionEnd(tb, offset);
	// data
	offset = initTCPData(tb, offset);
	uintptr_t packetSize = finishInitTCPPacket(tb, offset, tcps->localAddress, tcps->remoteAddress);
	uintptr_t rwSize = packetSize;
	// write
	uintptr_t r = syncWriteFile(tcps->rawSocketHandle, tb, &rwSize);
	if(r == IO_REQUEST_FAILURE || rwSize != packetSize){
		return 0;
	}
	return 1;
}

// return packet ACK number - window ACK number
static uintptr_t receiveTCPACK(const TCPHeader *rawBuffer, TCPTransmitWindow *transmitWindow){
	uintptr_t mss = transmitWindow->maxSegmentSize, ws = transmitWindow->lastWindowScale;
	int sack = 0;
	int ok = parseTCPOptions(rawBuffer, &mss, &ws, &sack);
	if(!ok){
		printk("TCP option error\n");
		return 0;
	}
	// modify window state
	uintptr_t ackSize = 0;
	if(rawBuffer->flags.ack){
		ackSize = ackTransmitWindow(transmitWindow, getAcknowledgeNumber(rawBuffer));
	}
	setTransmitWindow(transmitWindow, changeEndian16(rawBuffer->windowsSize), ws);
	if(rawBuffer->flags.syn){ // the value cannot be changed after handshake
		setTransmitSegmentSize(transmitWindow, mss);
	}
	return ackSize;
}

// return number of bytes appended to window
static uintptr_t receiveTCPData(TCPHeader *rawBuffer, uintptr_t readSize, TCPReceiveWindow *receiveWindow){
	uint32_t remoteSeqNumber = changeEndian32(rawBuffer->sequenceNumber);
	if(rawBuffer->flags.syn){
		synReceiveWindow(receiveWindow, remoteSeqNumber + 1);
		return 0;
	}
	else{
		uintptr_t dataBegin = getTCPHeaderSize(rawBuffer);
		return copyTCPReceiveWindow(receiveWindow, remoteSeqNumber,
			((const uint8_t*)rawBuffer) + dataBegin, readSize - dataBegin);
	}
}

static uintptr_t receiveTCPPacket(TCPSocket *tcps, uintptr_t readSize, uintptr_t *ackSize){
	if(tcps->receiveBuffer->flags.syn){
		return 0;
	}
	*ackSize = receiveTCPACK(tcps->receiveBuffer, &tcps->transmitWindow);
	return receiveTCPData(tcps->receiveBuffer, readSize, &tcps->receiveWindow);
}

static int receiveTCPSYNACK(TCPSocket *tcps){
	uintptr_t rwSize = tcps->rawBufferSize;
	TCPHeader *rb = tcps->receiveBuffer;
	// TODO: retransmission
	uintptr_t r = syncReadFile(tcps->rawSocketHandle, rb, &rwSize);
	// check header size and checksum in validateTCPPacket()
	if(r == IO_REQUEST_FAILURE || rwSize < getTCPHeaderSize(rb)){
		printk("TCP handshake 2 size error\n");
		return 0;
	}
	if(rb->flags.syn == 0 || rb->flags.ack == 0){
		printk("TCP handshake 2 flags error\n", rb->flags.value);
		return 0;
	}
	if(receiveTCPACK(rb, &tcps->transmitWindow) != 1){
		printk("TCP handshake 2 ACK number error %u %u\n",
			tcps->transmitWindow.sequenceBegin, getAcknowledgeNumber(rb));
		return 0;
	}
	receiveTCPData(rb, rwSize, &tcps->receiveWindow);
	return 1;
}

static int tcpHandShake(TCPSocket *tcps){
	// 1. SYN
	if(transmitTCPSYNPacket(tcps, 0) == 0){
		printk("TCP handshake 1 error");
		return 0;
	}
	// 2. SYN ACK
	if(receiveTCPSYNACK(tcps) == 0){
		return 0;
	}// TODO: retransmission
	// 3. ACK
	uintptr_t transmitSize;
	if(transmitTCPDataPacket(tcps, 1, &transmitSize) == 0){
		printk("TCP handshake 3 error");
		return 0;
	}
	assert(transmitSize == 0);
	return 1;
}

static int tcpLoop(TCPSocket *tcps){
	int errorFlag = 0, closeFlag = 0;
	// read socket
	uintptr_t readSocketIO = IO_REQUEST_FAILURE;
	// RW request
	uintptr_t rwRequestFIFOIO = IO_REQUEST_FAILURE;
	// ACK timer
	int needACKFlag = 0;
	const int ackDelayTime = 40;
	uintptr_t ackTimerIO = IO_REQUEST_FAILURE;
	// receive window timer
	int needReadFlag = 0;
	const int readDelayTime = 20;
	uintptr_t readTimerIO = IO_REQUEST_FAILURE;
	// retransmit timer
	int needRetrainsmitFlag = 0;
	const int retrainsmitDelayTime = 200;
	uintptr_t retransmitTimerIO = IO_REQUEST_FAILURE;
	const int maxTransmitCount = 5;
	int transmitCount = 0;

	while(errorFlag == 0 && closeFlag == 0){
		RWTCPRequest rwTCPRequest;
		if(readSocketIO == IO_REQUEST_FAILURE){
			readSocketIO = systemCall_readFile(tcps->rawSocketHandle, tcps->receiveBuffer, tcps->rawBufferSize);
			//if(readSocketIO == IO_REQUEST_FAILURE){
			//}
		}
		if(rwRequestFIFOIO == IO_REQUEST_FAILURE){
			rwRequestFIFOIO = systemCall_readFile(tcps->rwRequestFIFOHandle, &rwTCPRequest, sizeof(rwTCPRequest));
		}
		if(needACKFlag && ackTimerIO == IO_REQUEST_FAILURE){
			ackTimerIO = systemCall_setAlarm(ackDelayTime, 0);
		}
		if(needReadFlag && readTimerIO == IO_REQUEST_FAILURE){
			readTimerIO = systemCall_setAlarm(readDelayTime, 0);
		}
		if(needRetrainsmitFlag && retransmitTimerIO == IO_REQUEST_FAILURE){
			retransmitTimerIO = systemCall_setAlarm(retrainsmitDelayTime, 0);
		}

		uintptr_t readSize;
		const uintptr_t r = systemCall_waitIOReturn(UINTPTR_NULL, 1, &readSize);
		if(r == IO_REQUEST_FAILURE){
			printk("warning: TCP task failed to wait IO\n");
			continue;
		}
		// packet
		if(r == readSocketIO){
			readSocketIO = IO_REQUEST_FAILURE;
			uintptr_t ackSize;
			uintptr_t extendSize = receiveTCPPacket(tcps, readSize, &ackSize);
			if(extendSize > 0){
				copyTCPReceiveBuffer(&tcps->receiveWindow, 0);
				needReadFlag = 1;
				needACKFlag = 1;
			}
			//TODO: needTransmitFlag = 1;
			if(ackSize > 0){ // refill the transmit window
				uintptr_t transmitSize;
				transmitTCPDataPacket(tcps, 0, &transmitSize);
				if(transmitSize > 0){
					needACKFlag = 0;
				}
			}
			continue;
		}
		// system call
		if(r == rwRequestFIFOIO){
			rwRequestFIFOIO = IO_REQUEST_FAILURE;
			if(readSize != sizeof(rwTCPRequest)){
				printk("warning: wrong size of RWTCPRequest %x\n", readSize);
				continue;
			}
			if(rwTCPRequest.rwfr == NULL){
				// socket is closing
				break;
			}
			if(rwTCPRequest.isWrite){
				int ok = pushTCPTransmitBuffer(&tcps->transmitWindow, rwTCPRequest.buffer, rwTCPRequest.bufferSize);
				if(ok){
					completeRWFileIO(rwTCPRequest.rwfr, rwTCPRequest.bufferSize, 0);
				}
				else{
					completeRWFileIO(rwTCPRequest.rwfr, 0, 0);
				}
				uintptr_t transmitSize;
				transmitTCPDataPacket(tcps, 0, &transmitSize);
				if(transmitSize > 0){
					needACKFlag = 0;
				}
			}
			else/*read*/{
				pushTCPReceiveBuffer(&tcps->receiveWindow,
					rwTCPRequest.rwfr, rwTCPRequest.buffer, rwTCPRequest.bufferSize);
				copyTCPReceiveBuffer(&tcps->receiveWindow, 0);
				needReadFlag = 1;
			}
			continue;
		}
		// delayed ACK
		if(r == ackTimerIO){
			ackTimerIO = IO_REQUEST_FAILURE;
			if(needACKFlag){
				needACKFlag = 0;
				uintptr_t transmitSize;
				transmitTCPDataPacket(tcps, 1, &transmitSize);
			}
			continue;
		}
		// delayed system call return
		if(r == readTimerIO){
			readTimerIO = IO_REQUEST_FAILURE;
			if(needReadFlag){
				needReadFlag = 0;
				copyTCPReceiveBuffer(&tcps->receiveWindow, 1);
			}
			continue;
		}
		// retransmit
		if(r == retransmitTimerIO){
			retransmitTimerIO = IO_REQUEST_FAILURE;
			if(needRetrainsmitFlag){
				needRetrainsmitFlag = 0;
			}
		}
		// write IP socket is synchronous
		printk("warning: unknown IO handle %x in TCP task\n", r);
	}

	if(readSocketIO !=  IO_REQUEST_FAILURE){
		cancelOrWaitIO(readSocketIO);
	}
	if(rwRequestFIFOIO != IO_REQUEST_FAILURE){
		cancelOrWaitIO(rwRequestFIFOIO);
	}
	if(ackTimerIO != IO_REQUEST_FAILURE){
		cancelOrWaitIO(ackTimerIO);
	}
	if(readTimerIO != IO_REQUEST_FAILURE){
		cancelOrWaitIO(readTimerIO);
	}
	if(retransmitTimerIO != IO_REQUEST_FAILURE){
		cancelOrWaitIO(retransmitTimerIO);
	}

	if(errorFlag){
		return 0;
	}
	return 1;
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
	tcps->rwRequestFIFOHandle = syncOpenFIFOFile();
	EXPECT(tcps->rwRequestFIFOHandle != IO_REQUEST_FAILURE);
	tcps->rawSocketHandle = syncOpenFileN(tcpRawName, tcpRawNameLength, OPEN_FILE_MODE_0);
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
	tcps->receiveBuffer = allocateKernelMemory(tcps->rawBufferSize);
	EXPECT(tcps->receiveBuffer != NULL);
	tcps->transmitBuffer = allocateKernelMemory(tcps->rawBufferSize);
	EXPECT(tcps->transmitBuffer != NULL);
	initTCPTransmitWindow(&tcps->transmitWindow, 9999);
	int ok = initTCPReceiveWindow(&tcps->receiveWindow, DEFAULT_TCP_RECEIVE_WINDOW_SIZE);
	EXPECT(ok);

	releaseKernelMemory(tcpRawName);
	return tcps;
	//destroyTCPReceiveWindow(&tcps->receiveWindow);
	//destroyTCPTransmitWindow(&tcps->transmitWindow);
	ON_ERROR;
	releaseKernelMemory(tcps->transmitBuffer);
	ON_ERROR;
	releaseKernelMemory(tcps->receiveBuffer);
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
	destroyTCPTransmitWindow(&tcps->transmitWindow);
	destroyTCPReceiveWindow(&tcps->receiveWindow);
	releaseKernelMemory(tcps->receiveBuffer);
	releaseKernelMemory(tcps->transmitBuffer);
	uintptr_t r;
	r = syncCloseFile(tcps->rwRequestFIFOHandle);
	if(r == IO_REQUEST_FAILURE){
		printk("warning: cannot close TCP request FIFO\n");
		assert(0);
	}
	r = syncCloseFile(tcps->rawSocketHandle);
	if(r == IO_REQUEST_FAILURE){
		printk("warning: cannot close TCP raw socket\n");
		assert(0);
	}
	DELETE(tcps);
}

static int readTCPSocket(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t size){
	TCPSocket *tcps = getFileInstance(of);
	return writeRWTCPRequest(tcps->rwRequestFIFOHandle, rwfr, buffer, size, 0);
	// see tcpLoop()
}

static int writeTCPSocket(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t size){
	TCPSocket *tcps = getFileInstance(of);
	return writeRWTCPRequest(tcps->rwRequestFIFOHandle, rwfr, (uint8_t*)buffer, size, 1);
	// see tcpLoop()
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
	ff.read = readTCPSocket;
	ff.write = writeTCPSocket;
	ff.close = closeTCP;
	completeOpenFile(arg->ofr, tcps, &ff);

	tcpLoop(tcps);

	deleteTCPSocket(tcps);
	systemCall_terminate();

	ON_ERROR;
	deleteTCPSocket(tcps);
	ON_ERROR;
	failOpenFile(arg->ofr);
	systemCall_terminate();
}

static int openTCPClient(
	OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength,
	__attribute__((__unused__)) OpenFileMode ofm
){
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

static int openTCPRaw(
	OpenFileRequest *ofr, const char *fileName, uintptr_t nameLength,
	__attribute__((__unused__)) OpenFileMode ofm
){
	TCPRawSocket *NEW(tcps);
	initIPSocket(
		&tcps->ipSocket, tcps, IP_DATA_PROTOCOL_TCP,
		transmitIPV4Packet, filterTCPPacket, receiveIPV4Packet, deleteTCPRawSocket
	);
	int ok = scanIPSocketArguments(&tcps->ipSocket, fileName, nameLength);
	EXPECT(ok);
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
	sleep(3000);
	printk("test TCP client\n");
	int ok = waitForFirstResource("tcpclient", RESOURCE_FILE_SYSTEM, matchName);
	assert(ok);
	const char *target = "tcpclient:192.168.56.1:59999;srcport=59998";
	uintptr_t tcp = syncOpenFileN(target, strlen(target), OPEN_FILE_MODE_0);
	assert(tcp != IO_REQUEST_FAILURE);
	while(1){
		char buffer[16];
		uintptr_t readSize = sizeof(buffer);
		uintptr_t r = syncReadFile(tcp, buffer, &readSize);
		assert(r != IO_REQUEST_FAILURE);
		printk("%d: ", readSize);
		printkString(buffer, readSize);
		printk("\n");
		r = syncWriteFile(tcp, buffer, &readSize);
		assert(r != IO_REQUEST_FAILURE);
	}
	uintptr_t r;
	r = syncCloseFile(tcp);
	assert(r != IO_REQUEST_FAILURE);
}

#endif
