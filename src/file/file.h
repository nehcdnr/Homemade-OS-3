#include<std.h>

// disk driver interface
uintptr_t systemCall_rwDisk(int driver,
	uintptr_t buffer, uint64_t lba, uint32_t sectorCount,
	uint32_t diskCode, int isWrite
);
uintptr_t systemCall_rwDiskSync(int driver,
	uintptr_t buffer, uint64_t lba, uint32_t sectorCount,
	uint32_t diskCode, int isWrite
);

struct InterruptParam;
void rwDiskArgument(struct InterruptParam *p,
	uintptr_t *buffer, uint64_t *lba, uint32_t *sectorCount,
	uint32_t *diskCode, int *isWrite
);

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
#define MAX_DISK_TYPE (0x10)
typedef enum MBR_SystemID DiskPartitionType;

int addDiskPartition(
	DiskPartitionType systemID, const char *driverName, int diskDriver,
	uint64_t startLBA, uint64_t sectorCount, uint32_t sectorSize,
	uint32_t diskCode
);
//int removeDiskPartition(int diskDriver, uint32_t diskCode);

void readPartitions(const char *driverName, int diskDriver, uint32_t diskCode,
	uint64_t lba, uint64_t sectorCount, uint32_t sectorSize);

uintptr_t systemCall_discoverDisk(DiskPartitionType diskType);

// file interface
int addFileSystem(int fileService, const char *name, size_t nameLength);
uintptr_t systemCall_discoverFileSystem(const char* name, int nameLength);

// FAT32
void fatDriver(void);
