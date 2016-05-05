#include"std.h"
#include"file/file.h"

typedef union{
	uint32_t value;
	uint8_t bytes[4];
}IPV4Address;

#define ANY_IPV4_ADDRESS ((IPV4Address)(uint32_t)0)

enum IPDataProtocol{
	IP_DATA_PROTOCOL_ICMP = 1,
	IP_DATA_PROTOCOL_TCP = 6,
	IP_DATA_PROTOCOL_UDP = 17,
	IP_DATA_PROTOCOL_TEST253 = 253,
	IP_DATA_PROTOCOL_TEST254 = 254
};

// big endian and most significant bit comes first
typedef struct{
	uint8_t headerLength: 4;
	uint8_t version: 4;
	uint8_t congestionNotification: 2;
	uint8_t differentiateService: 6;
	uint16_t totalLength;
	uint16_t identification;
	uint8_t fragmentOffsetHigh: 5;
	uint8_t flags: 3;
	uint8_t fragmentOffsetLow;
	uint8_t timeToLive;
	uint8_t protocol;
	uint16_t headerChecksum;
	IPV4Address source;
	IPV4Address destination;
	//uint8_t options[];
	//uint8_t payload[];
}IPV4Header;
/*
example
45 00 00 3c 1a 02 00 00 80 01
2e 6e c0 a8 38 01 c0 a8 38 ff
*/
#define MAX_IP_PACKET_SIZE ((1 << 16) - 1)

// does not check dataLength
void initIPV4Header(
	IPV4Header *h, uint16_t dataLength, IPV4Address srcAddress, IPV4Address dstAddress,
	enum IPDataProtocol dataProtocol
);
uintptr_t getIPHeaderSize(const IPV4Header *h);
uintptr_t getIPDataSize(const IPV4Header *h);

// return little endian number which can be greater than 0xffff
uint32_t calculatePseudoIPChecksum(const IPV4Header *h);

typedef struct IPSocket IPSocket;

typedef IPV4Header *CreatePacket(IPSocket *ipSocket, const uint8_t *buffer, uintptr_t bufferLength);
typedef int ReceivePacket(
	IPSocket *ipSocket,
	uint8_t *buffer, uintptr_t *bufferSize,
	const IPV4Header *packet, uintptr_t packetSize
);
typedef void DeletePacket(/*IPSocket *ipSocket, */IPV4Header *packet);
// one receive queue & task for every socket
struct IPSocket{
	void *instance;
	IPV4Address localAddress;
	IPV4Address remoteAddress;
	CreatePacket *createPacket;
	ReceivePacket *receivePacket;
	DeletePacket *deletePacket;

	struct RWIPQueue *receive, *transmit;
};

int initIPSocket(IPSocket *socket, void *inst, const unsigned *src, CreatePacket *c, ReceivePacket *r, DeletePacket *d);

int isIPV4PacketAcceptable(const IPSocket *ips, const IPV4Header *packet);

void destroyIPSocket(IPSocket *socket);

int createAddRWIPArgument(struct RWIPQueue *q, RWFileRequest *rwfr, IPSocket *ips, uint8_t *buffer, uintptr_t size);

int setIPAddress(IPSocket *ips, uintptr_t param, uint64_t value);

// udp.c

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

UDPIPHeader *createUDPIPPacket(
	const uint8_t *data, uint16_t dataLength,
	IPV4Address localAddr, uint16_t localPort,
	IPV4Address remoteAddr, uint16_t remotePort
);

void initUDPIPHeader(
	UDPIPHeader *h, uint16_t dataLength,
	IPV4Address localAddr, uint16_t localPort,
	IPV4Address remoteAddr, uint16_t remotePort
);

void initUDP(void);

// dhcp.c
typedef struct{
	Spinlock lock;
	IPV4Address bindingAddress;
	IPV4Address subnetMask;
}IPConfig;

typedef struct DHCPClient DHCPClient;

DHCPClient *createDHCPClient(uintptr_t devFileHandle, IPConfig *ipConfig);
