#include"common.h"

#define TO_BIG_ENDIAN_16(X) ((uint16_t)((((X) << 8) & 0xff00) | (((X) >> 8) & 0xff)))

typedef uint16_t EtherType;

#define ETHERTYPE_0 TO_BIG_ENDIAN_16(0)
#define ETHERTYPE_IPV4 TO_BIG_ENDIAN_16(0x0800)
#define ETHERTYPE_ARP TO_BIG_ENDIAN_16(0x0806)
#define ETHERTYPE_VLAN_TAG TO_BIG_ENDIAN_16(0x8100)


#define MAC_ADDRESS_SIZE (6)
void toMACAddress(volatile uint8_t *outAddress, uint64_t macAddress);
