#include"std.h"
#include"multiprocessor/processorlocal.h"
#include"task/task.h"
#include"network.h"

struct DHCPClient{
	uintptr_t deviceFile;
	uint64_t macAddress;
	IPConfig *outputIPConfig;
};

static void dhcpClientTask(void *arg);

DHCPClient *createDHCPClient(uintptr_t devFileHandle, IPConfig *ipConfig){
	DHCPClient *NEW(dhcp);
	EXPECT(dhcp != NULL);
	dhcp->deviceFile = devFileHandle;
	dhcp->outputIPConfig = ipConfig;
	uintptr_t r = syncGetFileParameter(devFileHandle, FILE_PARAM_SOURCE_ADDRESS, &dhcp->macAddress);
	EXPECT(r != IO_REQUEST_FAILURE);
	Task *t = createSharedMemoryTask(dhcpClientTask, &dhcp, sizeof(dhcp), processorLocalTask());
	EXPECT(t != NULL);
	resume(t);
	return dhcp;
	// terminate t
	ON_ERROR;
	// get MAC address
	ON_ERROR;
	DELETE(dhcp);
	ON_ERROR;
	return NULL;
}

enum DHCPMessageType{
	DHCP_TYPE_DISCOVER = 1,
	DHCP_TYPE_OFFER = 2,
	DHCP_TYPE_REQUEST = 3,
	// DHCP_TYPE_DECLINE = 4,
	DHCP_TYPE_ACK = 5
	// DHCP_TYPE_NACK = 6,
	// DHCP_TYPE_RELEASE = 7,
	// DHCP_TYPE_INFORM = 8
};

#pragma pack(1)

enum DHCPOptionCode{
	DHCP_OPTION_SUBNET_MASK = 1,
	DHCP_OPTION_ROUTER = 3,
	DHCP_OPTION_DNS_SERVER = 6,
	DHCP_REQUESTED_IP_ADDRESS = 50,
	DHCP_OPTION_LEASE_TIME = 51,
	DHCP_OPTION_MSG_TYPE = 53,
	DHCP_OPTION_SERVER = 54,
	DHCP_OPTION_PARAMETER = 55,
	DHCP_OPTION_END = 255
};

typedef struct __attribute__((__packed__)){
	uint8_t optionCode, optionSize;
	union{
		uint8_t parameter[0];
		uint8_t msgType;
		uint32_t value32;
		IPV4Address ipv4Address;
	};
}DHCPOption;

#pragma pack()

static_assert(sizeof(DHCPOption) == 6);

#define SET_DHCP_OPTION(O, OPTION_CODE, MEMBER, VALUE) do{\
	(O)->optionCode = (OPTION_CODE);\
	(O)->optionSize = sizeof((O)->MEMBER);\
	(O)->MEMBER = (VALUE);\
}while(0)

static DHCPOption *nextOption(DHCPOption *o){
	uintptr_t p = ((uintptr_t)o->parameter) + o->optionSize;
	return (DHCPOption*)p;
}

static DHCPOption *setDHCPOption_MsgType(DHCPOption *o, enum DHCPMessageType t){
	SET_DHCP_OPTION(o, DHCP_OPTION_MSG_TYPE, msgType, t);
	return nextOption(o);
}
/*
static DHCPOption *setDHCPOption_Address(DHCPOption *o, enum DHCPOptionCode optCode, IPV4Address addr){
	SET_DHCP_OPTION(o, optCode, ipv4Address, addr);
	return nextOption(o);
}

static DHCPOption *setDHCPOption_Router(DHCPOption *o, IPV4Address router){
	return setDHCPOption_Address(o, DHCP_OPTION_ROUTER, router);
}

static DHCPOption *setDHCPOption_Server(DHCPOption *o, IPV4Address server){
	return setDHCPOption_Address(o, DHCP_OPTION_SERVER, server);
}

static DHCPOption *setDHCPOption_LeaseTime(DHCPOption *o, uint32_t leaseTime){
	SET_DHCP_OPTION(o, DHCP_OPTION_LEASE_TIME, value32, leaseTime);
	return nextOption(o);
}
*/

static_assert(sizeof(enum DHCPOptionCode) == sizeof(unsigned));

static DHCPOption *setDHCPOption_Parameter(DHCPOption *o, uint8_t paramCount, ...){
	o->optionCode = DHCP_OPTION_PARAMETER;
	o->optionSize = paramCount;
	va_list args;
	va_start(args, paramCount);
	uint8_t i;
	for(i = 0; i < paramCount; i++){
		o->parameter[i] = va_arg(args, unsigned);
	}
	va_end(args);
	return nextOption(o);
}

static DHCPOption *setDHCPOption_End(DHCPOption *o){
	o->optionCode = DHCP_OPTION_END;
	o->optionSize = 0;
	return nextOption(o);
}

typedef struct{
	UDPIPHeader udpipHeader;
	uint8_t operationCode; // 1: request; 2: reply
	uint8_t hardwareType; // 1: Ethernet
	uint8_t hardwareAddressLength; // 6
	uint8_t hops;
	uint32_t transactionID;
	uint16_t seconds;
	uint16_t flags;
	IPV4Address clientAddress;
	IPV4Address yourAddress;
	IPV4Address serverAddress;
	IPV4Address gatewayAddress;
	uint8_t clientHardwareAddress[16];
	uint8_t unused[192];
	uint8_t magic[4]; // 0x63825363
	DHCPOption option[];
}DHCPPacket;

static_assert(sizeof(DHCPPacket) == 268);

static void initDHCPDiscoverPacket(DHCPPacket *p, uint32_t id, uint64_t macAddress){
	p->operationCode = 1;
	p->hardwareType = 1;
	p->hardwareAddressLength = 6;
	p->hops = 0;
	p->transactionID = id;
	p->seconds = 0;
	p->flags = changeEndian16(0x8000);
	p->clientAddress = (IPV4Address)(uint32_t)0;
	p->yourAddress = (IPV4Address)(uint32_t)0;
	p->serverAddress = (IPV4Address)(uint32_t)0;
	p->gatewayAddress = (IPV4Address)(uint32_t)0;
	memset(p->clientHardwareAddress, 0, sizeof(p->clientHardwareAddress));
	toMACAddress(p->clientHardwareAddress, macAddress);
	memset(p->unused, 0, sizeof(p->unused));
	p->magic[0] = 0x63;
	p->magic[1] = 0x82;
	p->magic[2] = 0x53;
	p->magic[3] = 0x63;
}

#define MAX_DHCP_OPTION_COUNT (10)
// return the sleep time till the next renewal
static int dhcpTransaction(DHCPClient *dhcp, uint32_t id){
	DHCPPacket *packet = allocateKernelMemory(sizeof(*packet) + sizeof(DHCPOption) * MAX_DHCP_OPTION_COUNT);
	EXPECT(packet != NULL);
	initDHCPDiscoverPacket(packet, id, dhcp->macAddress);
	DHCPOption *opt = packet->option;
	opt = setDHCPOption_MsgType(opt, DHCP_TYPE_DISCOVER);
	opt = setDHCPOption_Parameter(opt, 3, DHCP_OPTION_SERVER, DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_DNS_SERVER);
	opt = setDHCPOption_End(opt);
	uintptr_t dataSize = ((uintptr_t)opt) - ((uintptr_t)packet->udpipHeader.udp.payload);
	uintptr_t packetSize = ((uintptr_t)opt) - ((uintptr_t)packet);
	initUDPIPHeader(
		&packet->udpipHeader, dataSize,
		(IPV4Address)(uint32_t)0, 68,
		(IPV4Address)(uint32_t)0xffffffff, 67
	);
	uintptr_t rwSize = packetSize;
	uintptr_t r = syncWriteFile(dhcp->deviceFile, packet, &rwSize);
	EXPECT(r != IO_REQUEST_FAILURE && packetSize != rwSize);
	r = syncReadFile(dhcp->deviceFile, packet, &rwSize);  // TODO: udp read
	EXPECT(r != IO_REQUEST_FAILURE);
	printk("\n\nrw size = %u\n\n", rwSize);
	return 100000;
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	return 200000;
}

static void dhcpClientTask(void *arg){
	DHCPClient *dhcp = *(DHCPClient**)arg;
	printk("DHCP client started\n");
	uint32_t id0 = ((uint32_t)&arg);
	uint32_t i;
	for(i = 0; 1; i = (i + 1) % 10){
		uintptr_t sleepTime = dhcpTransaction(dhcp, id0 + i);
		sleep(sleepTime);
	}
	systemCall_terminate();
}
