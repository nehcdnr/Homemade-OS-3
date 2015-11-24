#include<std.h>
#include"memory/memory.h"

// disk driver

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
	DiskPartitionType systemID, const char *driverName,
	uint64_t startLBA, uint64_t sectorCount, uintptr_t sectorSize,
	uintptr_t diskCode
);
//int removeDiskPartition(int diskDriver, uintptr_t diskCode);

void readPartitions(const char *driverName, uintptr_t diskCode,
	uint64_t lba, uint64_t sectorCount, uintptr_t sectorSize);

uintptr_t systemCall_discoverDisk(DiskPartitionType diskType);

// file interface
typedef struct IORequest IORequest;
typedef IORequest *OpenFileFunction(const char *fileName, uintptr_t nameLength);

int addFileSystem(OpenFileFunction openFileFunction, const char *name, size_t nameLength);
uintptr_t systemCall_discoverFileSystem(const char* name, int nameLength);

// if openFile failed, return IO_REQUEST_FAILURE
uintptr_t systemCall_openFile(const char *fileName, uintptr_t fileNameLength);
uintptr_t syncOpenFile(const char *fileName);
uintptr_t syncOpenFileN(const char *fileName, uintptr_t fileNameLength);
uintptr_t systemCall_readFile(uintptr_t handle, void *buffer, uintptr_t bufferSize);
uintptr_t syncReadFile(uintptr_t handle, void *buffer, uintptr_t *bufferSize);
uintptr_t systemCall_writeFile(uintptr_t handle, const void *buffer, uintptr_t bufferSize);
//uintptr_t syncWriteFile(uintptr_t handle, const void *buffer, uintptr_t *bufferSize);
uintptr_t systemCall_seekFile(uintptr_t handle, uint64_t position);
uintptr_t syncSeekFile(uintptr_t handle, uint64_t position);
uintptr_t systemCall_seekReadFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t bufferSize);
uintptr_t syncSeekReadFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t *bufferSize);
uintptr_t systemCall_seekWriteFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t bufferSize);
uintptr_t systemCall_sizeOfFile(uintptr_t handle);
uintptr_t syncSizeOfFile(uintptr_t handle, uint64_t *size);
uintptr_t systemCall_closeFile(uintptr_t handle);
uintptr_t syncCloseFile(uintptr_t handle);

// call unmapPages(kernelLinear, mappedPage) to release
int mapBufferToKernel(const void *buffer, uintptr_t size, void **mappedPage, void **mappedBuffer);
PhysicalAddressArray *reserveBufferPages(void *buffer, uintptr_t bufferSize, uintptr_t *bufferOffset);

typedef struct SystemCallTable SystemCallTable;
void initFile(SystemCallTable *s);

typedef struct OpenFileRequest OpenFileRequest;

typedef struct{
	//IORequest *(*open)(const char *fileName, uintptr_t nameLength);
	OpenFileFunction *open;
	IORequest *(*read)(OpenFileRequest *ofr, uint8_t *buffer, uintptr_t bufferSize);
	IORequest *(*write)(OpenFileRequest *ofr, const uint8_t *buffer, uintptr_t bufferSize);
	IORequest *(*seek)(OpenFileRequest *ofr, uint64_t position/*, whence*/);
	IORequest *(*seekRead)(OpenFileRequest *ofr, uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
	IORequest *(*seekWrite)(OpenFileRequest *ofr, const uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
	IORequest *(*sizeOf)(OpenFileRequest *ofr);
	IORequest *(*close)(OpenFileRequest *ofr);
	// If the argument is not valid, return 0.
	// Otherwise, the argument of isValidFile will be passed to the above functions
	int (*isValidFile)(OpenFileRequest *ofr);
}FileFunctions;

// use macro to check number of arguments
#define INITIAL_FILE_FUNCTIONS(OPEN, READ, WRITE, SEEK, SEEK_READ, SEEK_WRITE, SIZE_OF, CLOSE, IS_VALID_FILE) \
	{OPEN, READ, WRITE, SEEK, SEEK_READ, SEEK_WRITE, SIZE_OF, CLOSE, IS_VALID_FILE}

typedef struct Task Task;
struct OpenFileRequest{
	void *instance;
	uintptr_t handle;
	//Task *task;
	FileFunctions fileFunctions;
	OpenFileRequest *next, **prev;
};
void initOpenFileRequest(
	OpenFileRequest *ofr,
	void *instance,
	//Task *task,
	const FileFunctions *fileFunctions
);

typedef struct OpenFileManager OpenFileManager;
OpenFileManager *createOpenFileManager(void);
void deleteOpenFileManager(OpenFileManager *ofm);
int addOpenFileManagerReference(OpenFileManager *ofm, int n);
void addToOpenFileList(OpenFileManager *ofm, OpenFileRequest *ofr);
void removeFromOpenFileList(OpenFileManager *ofm, OpenFileRequest *ofr);
uintptr_t getFileHandle(OpenFileRequest *ofr);
void closeAllOpenFileRequest(OpenFileManager *ofm);

extern OpenFileManager *globalOpenFileManager; // see taskmanager.c

// FAT32
void fatService(void);

// kernel file
void kernelFileService(void);
