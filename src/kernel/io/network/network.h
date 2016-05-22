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

typedef IPV4Header *CreatePacket(
	IPSocket *ipSocket, IPV4Address src, IPV4Address dst,
	const uint8_t *buffer, uintptr_t bufferLength
);
typedef int ReceivePacket(
	IPSocket *ipSocket,
	uint8_t *buffer, uintptr_t *bufferSize,
	const IPV4Header *packet, uintptr_t packetSize, int isBroadcast
);
typedef void DeletePacket(/*IPSocket *ipSocket, */IPV4Header *packet);
// one receive queue & task for every socket
struct IPSocket{
	void *instance;
	IPV4Address localAddress;
	uint16_t localPort;
	IPV4Address remoteAddress;
	uint16_t remotePort;
	int bindToDevice;
	char deviceName[MAX_FILE_ENUM_NAME_LENGTH];
	uintptr_t deviceNameLength;

	CreatePacket *createPacket;
	ReceivePacket *receivePacket;
	DeletePacket *deletePacket;

	struct RWIPQueue *receive, *transmit;
};

void initIPSocket(IPSocket *socket, void *inst, CreatePacket *c, ReceivePacket *r, DeletePacket *d);
int scanIPSocketArguments(IPSocket *socket, const char *arg, uintptr_t argLength);
int startIPSocket(IPSocket *socket);

void setIPSocketLocalAddress(IPSocket *s, IPV4Address a);
void setIPSocketRemoteAddress(IPSocket *s, IPV4Address a);
void setIPSocketBindingDevice(IPSocket *s, const char *deviceName, uintptr_t nameLength);

int isIPV4PacketAcceptable(const IPSocket *ips, const IPV4Header *packet, int isBroadcast);

void destroyIPSocket(IPSocket *socket);

int createAddRWIPArgument(struct RWIPQueue *q, RWFileRequest *rwfr, IPSocket *ips, uint8_t *buffer, uintptr_t size);

int setIPAddress(IPSocket *ips, uintptr_t param, uint64_t value);

// udp.c

void initUDP(void);

// dhcp.c
typedef struct{
	IPV4Address localAddress;
	IPV4Address subnetMask;
	// optional
	IPV4Address gateway, dhcpServer, dnsServer;
}IPConfig;

typedef struct DHCPClient DHCPClient;

DHCPClient *createDHCPClient(const FileEnumeration *fe, IPConfig *ipConfig, Spinlock *ipConfigLock, uint64_t macAddress);

//arp.c

typedef struct ARPServer ARPServer;

ARPServer *createARPServer(const FileEnumeration *fe, IPConfig *ipConfig, Spinlock *ipConfigLock, uint64_t macAddress);
