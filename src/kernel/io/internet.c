#include"std.h"
#include"resource/resource.h"
#include"task/task.h"

typedef struct{
	uint8_t version: 4;
	uint8_t headerLength: 4;
	uint8_t differentiateService: 6;
	uint8_t congestionNotification: 2;
	uint16_t totalLength;
	uint16_t identification;
	uint16_t flags: 3;
	uint16_t fragmentOffset: 13;
	uint8_t timeToLive;
	uint8_t protocol;
	uint16_t headerChecksum;
	uint32_t source;
	uint32_t destination;
	//uint8_t options[];
	uint8_t payload[];
}IPV4Header;

static_assert(sizeof(IPV4Header) / sizeof(uint32_t) == 5);

// return value in big endian
static uint16_t calculateIPChecksum(const IPV4Header *h){
	uint32_t cs = 0;
	uintptr_t i;
	for(i = 0; i < 2 * (uintptr_t)h->headerLength; i++){
		cs += (uint32_t)changeEndian16(((uint16_t*)h)[i]);
	}
	while((cs & 0xffff) != cs){
		cs = (cs & 0xffff) + (cs >> 16);
	}
	return changeEndian16(cs ^ 0xffff);
}

// TODO: change endian of srcAddress, dstAddress
static void initIPV4Header(IPV4Header *h, uint16_t dataLength, uint32_t srcAddress, uint32_t dstAddress){
	h->version = 4;
	h->headerLength = sizeof(IPV4Header) / sizeof(uint32_t);
	h->differentiateService = 0;
	h->congestionNotification = 0;
	h->totalLength = changeEndian16(sizeof(IPV4Header) + dataLength);
	h->identification = changeEndian16(0);
	h->flags = 2; // 2 = don't fragment; 4 = more fragment
	h->fragmentOffset = 0; // byte order?
	h->timeToLive = 32;
	h->protocol = 254; // 1 = ICMP; 6 = TCP; 11 = UDP; 253 = testing; 254 = testing
	h->headerChecksum = 0;
	h->source = srcAddress;
	h->destination = dstAddress;

	h->headerChecksum = calculateIPChecksum(h);
	assert(calculateIPChecksum(h) == 0);
}

static IPV4Header *createIPV4Packet(uint16_t dataLength, uint32_t srcAddress, uint32_t dstAddress){
	IPV4Header *NEW(h);
	if(h == NULL){
		return NULL;
	}
	initIPV4Header(h, dataLength, srcAddress, dstAddress);
	return h;
}

static uintptr_t enumNextDataLinkDevice(uintptr_t f, const char *namePattern, FileEnumeration *fe){
	return enumNextResource(f, fe, (uintptr_t)namePattern, matchWildcardName);
}

void internetService(void){
	//const char *testSource = "192.168.1.10";
	uintptr_t enumDataLink = syncEnumerateFile(resourceTypeToFileName(RESOURCE_DATA_LINK_DEVICE));
	while(1){
		FileEnumeration fe;
		enumNextDataLinkDevice(enumDataLink, "*", &fe);

		OpenFileMode ofm = OPEN_FILE_MODE_0;
		ofm.writable = 1;
		uintptr_t d = syncOpenFileN(fe.name, fe.nameLength, ofm);
		if(d == IO_REQUEST_FAILURE){
			printk("cannot open device file");
			printString(fe.name, fe.nameLength);
			printk("\n");
			continue;
		}
		/*
		uintptr_t mtu;
		uintptr_t r = syncWritableSizeOfFile(d, &mtu);
		if(r == IO_REQUEST_FAILURE){
			printk("cannot get writable size\n");
			continue;
		}
		printk("MTU = %u\n", mtu);
		*/
		IPV4Header *h = createIPV4Packet(100, 1234, 4567);
		if(h == NULL){
			printk("error\n");
		}
		else{
			DELETE(h);
		}
		while(1)
			sleep(10000);// TODO:
	}
	syncCloseFile(enumDataLink);
	systemCall_terminate();
}
