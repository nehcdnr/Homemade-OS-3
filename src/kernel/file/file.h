#include<std.h>

// disk driver interface
// IMPROVE: merge to file system call
uintptr_t systemCall_readWrite(int driver,
	uintptr_t buffer, uint64_t location, uintptr_t bufferSize,
	uintptr_t targetIndex, int isWrite
);
uintptr_t systemCall_readWriteSync(int driver,
	uintptr_t buffer, uint64_t location, uintptr_t bufferSize,
	uintptr_t targetIndex, int isWrite
);

struct InterruptParam;
void readWriteArgument(struct InterruptParam *p,
	uintptr_t *buffer, uint64_t *location, uintptr_t *bufferSize,
	uintptr_t *targetIndex, int *isWrite
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
	uint64_t startLBA, uint64_t sectorCount, uintptr_t sectorSize,
	uintptr_t diskCode
);
//int removeDiskPartition(int diskDriver, uintptr_t diskCode);

void readPartitions(const char *driverName, int diskDriver, uintptr_t diskCode,
	uint64_t lba, uint64_t sectorCount, uintptr_t sectorSize);

uintptr_t systemCall_discoverDisk(DiskPartitionType diskType);

// file interface
int addFileSystem(int fileService, const char *name, size_t nameLength);
uintptr_t systemCall_discoverFileSystem(const char* name, int nameLength);


uintptr_t systemCall_openFile(int fileService, const char *fileName, uintptr_t nameLength);
uintptr_t systemCall_readFile(int fileService, uintptr_t handle, void *buffer, uintptr_t bufferSize);
uintptr_t systemCall_writeFile(int fileService, uintptr_t handle, const void *buffer, uintptr_t bufferSize);
uintptr_t systemCall_seekFile(int fileService, uintptr_t handle, uint64_t position);
uintptr_t systemCall_closeFile(int fileService, uintptr_t handle);

typedef struct IORequest IORequest;
typedef struct InterruptParam InterruptParam;
typedef struct Task Task;
typedef struct{
	IORequest *(*open)(const char *fileName, uintptr_t nameLength);
	IORequest *(*read)(void *arg, uint8_t *buffer, uintptr_t bufferSize);
	IORequest *(*write)(void *arg, const uint8_t *buffer, uintptr_t bufferSize);
	IORequest *(*seek)(void *arg, uint64_t position);
	IORequest *(*close)(void *arg);
	// If the argument is not a valid handle, return NULL.
	// Otherwise, the return value of checkHandle will be passed to the above functions
	void *(*checkHandle)(uintptr_t handle, Task* task);
}FileFunctions;

uintptr_t dispatchFileSystemCall(InterruptParam *p, FileFunctions *f);

// FAT32
void fatService(void);

//kernel file
void kernelFileService(void);
