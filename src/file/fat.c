#include"common.h"
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
	uint32_t sectorsPerFAT2;
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

#pragma pack()

static_assert(sizeof(FATBootRecord) == 512);

void fatDriver(void){
	uintptr_t discoverFAT = systemCall_discoverDisk(MBR_FAT32);
	assert(discoverFAT != IO_REQUEST_FAILURE);
	int diskDriver;
	uintptr_t diskCode;
	while(1){
		uintptr_t discoverFAT2 = systemCall_waitIOReturn(2, (uintptr_t)&diskDriver, &diskCode);
		assert(discoverFAT == discoverFAT2);
		printk("fat received %u %u\n",diskDriver, diskCode);
	}
}
