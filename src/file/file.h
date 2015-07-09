#include<std.h>

#pragma pack(1)
struct MBR{
	uint8_t bootCode[446];
	struct PartitionEntry{
		uint8_t active; // 0x80 = active
		uint8_t startHead;
		uint16_t startSector: 6;
		uint16_t startCydlinder: 10;
		uint8_t systemID;
		uint8_t endHead;
		uint16_t endSector: 6;
		uint16_t endCylinder: 10;
		uint32_t startLBA;
		uint32_t sectorCount;
	}partition[4];
	uint16_t signature;
}__attribute__((__packed__));
#pragma pack()

#define MBR_SIGNATRUE (0xaa55)

enum MBR_SystemID{
	MBR_EMPTY = 0x00,
	MBR_FAT12 = 0x01,
	MBR_FAT16_DOS3 = 0x04,
	MBR_EXTENDED = 0x05,
	MBR_FAT16_DOS331 = 0x06,
	//MBR_NTFS = 0x07,
	//MBR_EXFAT = 0x07,
	MBR_FAT32 = 0x0b,
	MBR_FAT32_LBA = 0x0c,
	MBR_FAT16_LBA = 0x0e,
	MBR_EXTENDED_LBA = 0x0f
};


int addDiskPartition(
	enum MBR_SystemID systemID, int diskDriver,
	uint64_t startLBA, uint64_t sectorCount,
	uint32_t partitionIndex
);
void removeDiskPartition(int diskDriver, uint32_t partitionIndex);
void initFileSystemManager(void);

// FAT32
void fatDriver(void);
