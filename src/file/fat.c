#include"common.h"
#include"memory/memory.h"
#include"file.h"
#include"interrupt/systemcall.h"
#include"io/io.h"

#pragma pack(1)

typedef struct __attribute__((__packed__)){
	uint8_t driveNumber;
	uint8_t reserved2;
	uint8_t signature; // 0x28 or 0x29
	uint32_t partitionSerialNumber;
	uint8_t partitionName[11];
	uint8_t fatName[8];
}FATExtBootRecord;

typedef struct __attribute__((__packed__)){
	FATExtBootRecord ext;
	uint8_t bootCode[448];
}FAT12BootRecord;

static_assert(sizeof(FAT12BootRecord) == 512 - 36 - 2);

typedef struct __attribute((__packed__)){
	uint32_t sectorsPerFAT32;
	uint16_t flags; // bit 0~4: active FAT copies; bit 7: FAT mirroring disabled
	uint16_t version;
	uint32_t rootCluster;
	uint16_t fsInfoSector;
	uint16_t backupBootSector;
	uint8_t reserved[12];
	FATExtBootRecord ext;
	uint8_t bootCode[420];
}FAT32BootRecord;

static_assert(sizeof(FAT32BootRecord) == sizeof(FAT12BootRecord));

typedef struct __attribute__((__packed__)){
	uint8_t jmp[3];
	uint8_t oemName[8];
	uint16_t bytesPerSector;
	uint8_t sectorsPerCluster;
	uint16_t reservedSectorCount;
	uint8_t fatCount;
	uint16_t directoryEntryCount;
	uint16_t sectorCount;
	uint8_t mediaType;
	uint16_t sectorsPerFAT;
	uint16_t sectorsPerTrack;
	uint16_t headCount;
	uint32_t hiddenSectorCount;
	uint32_t SectorCount2;
	// extended boot record
	union{
		FAT12BootRecord ebr12;
		FAT32BootRecord ebr32;
	};
	uint16_t bootSignature; // 0xaa55
}FATBootRecord;

static_assert(sizeof(FATBootRecord) == 512);

typedef struct __attribute__((__packed__)){
	uint8_t fileName[11];
	uint8_t attribute;
	uint8_t reserved;
	uint8_t createDecisecond;
	uint16_t createTime; // h:m:s = 5:6:5
	uint16_t createDate; // y:m:d = 7:4:5
	uint16_t accessDate;
	uint16_t clusterHigh;
	uint16_t modifyTime;
	uint16_t modifyDate;
	uint16_t clusterLow;
	uint32_t fileSize;
}FATDirEntry;

static_assert(sizeof(FATDirEntry) == 32);

typedef struct __attribute__((__packed__)){
	uint8_t position;
	uint16_t fileName0[5];
	uint8_t attribute; // 0x0f
	uint8_t type; // 0
	uint8_t checksum;
	uint16_t fileName5[6];
	uint16_t reserved; // 0
	uint16_t fileName11[2];
}LongFATDirEntry;

static_assert(sizeof(LongFATDirEntry) == 32);

#pragma pack()

static struct SlabManager *slab = NULL;
static void readFAT(int diskDriver, uint64_t startLBA, uintptr_t diskCode, uintptr_t sectorSize){
	const size_t readSize = CEIL(sizeof(FATBootRecord), sectorSize);
	volatile FATBootRecord *br = systemCall_allocateHeap(readSize, KERNEL_NON_CACHED_PAGE);
	EXPECT(br != NULL);
	MEMSET0((FATBootRecord*)br);
	uintptr_t rwDisk = systemCall_rwDisk(diskDriver, (uintptr_t)br, startLBA, readSize / sectorSize, diskCode, 0);
	EXPECT(rwDisk != IO_REQUEST_FAILURE);
	EXPECT(br->ebr32.ext.signature == 0x28 || br->ebr32.ext.signature == 0x29);

	const uint32_t fatBeginLBA = startLBA + br->reservedSectorCount;
	const uint32_t sectorsPerFAT32 = br->ebr32.sectorsPerFAT32;
	//uint32_t bytesPerCluster = br->sectorsPerCluster * br->bytesPerSector;
	const uint32_t sectorsPerPage = PAGE_SIZE / sectorSize;
	//printk("%x %x %x %x\n",br->ebr32.rootCluster, fatBeginLBA, sectorsPerFAT32, bytesPerCluster);
	// allocate FAT
	size_t fatSize = sectorsPerFAT32 * br->bytesPerSector;

	uint32_t *fat = systemCall_allocateHeap(CEIL(fatSize, PAGE_SIZE), KERNEL_NON_CACHED_PAGE);
	EXPECT(fat != NULL);
	size_t p;
	for(p = 0; p * PAGE_SIZE < fatSize; p++){
		rwDisk = systemCall_rwDiskSync(diskDriver,
			((uintptr_t)fat) + PAGE_SIZE * p, fatBeginLBA + p * sectorsPerPage,
			sectorsPerPage, diskCode, 0);
		if(rwDisk == IO_REQUEST_FAILURE){
			printk("warning: failed to read FAT\n"); //TODO
			break;
		}
	}
	EXPECT(p * PAGE_SIZE >= fatSize);

	const unsigned fatEntryCount = sectorsPerFAT32 * (sectorSize / 4);
	for(p = 0; p < 50 && p < fatEntryCount; p++){
		printk("%x ", fat[p]);
	}
	printk("\nread fat ok\n");
	systemCall_releaseHeap(fat);
	systemCall_releaseHeap((void*)br);
	return;
	ON_ERROR;
	systemCall_releaseHeap(fat);
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	systemCall_releaseHeap((void*)br);
	ON_ERROR;
	printk("warning: read FAT32 failed\n");
}

void fatDriver(void){
	slab = createUserSlabManager(); // move it to user library
	uintptr_t discoverFAT = systemCall_discoverDisk(MBR_FAT32);
	assert(discoverFAT != IO_REQUEST_FAILURE);
	int diskDriver;
	uintptr_t startLBALow;
	uintptr_t startLBAHigh;
	uintptr_t diskCode;
	uintptr_t sectorSize;
	while(1){
		uintptr_t discoverFAT2 = systemCall_waitIOReturn(
			discoverFAT, 5,
			(uintptr_t)&diskDriver, &startLBALow, &startLBAHigh, &diskCode, &sectorSize);
		assert(discoverFAT == discoverFAT2);
		//printk("fat received %u %x %u %u\n", diskDriver, startLBALow, diskCode, sectorSize);
		readFAT(diskDriver, startLBALow + (((uint64_t)startLBAHigh) << 32), diskCode, sectorSize);
	}
}
