#include"memory/memory.h"
#include"io/io.h"
#include"multiprocessor/spinlock.h"
#include"interrupt/systemcall.h"
#include"multiprocessor/processorlocal.h"
#include"common.h"
#include"file.h"
#include"resource/resource.h"

// disk driver interface

void readWriteArgument(struct InterruptParam *p,
	uintptr_t *buffer, uint64_t *location, uintptr_t *bufferSize,
	uintptr_t *targetIndex, int *isWrite
){
	*buffer = SYSTEM_CALL_ARGUMENT_0(p);
	*location = SYSTEM_CALL_ARGUMENT_1(p) + (((uint64_t)SYSTEM_CALL_ARGUMENT_2(p)) << 32);
	*bufferSize = (SYSTEM_CALL_ARGUMENT_3(p) & 0x7fffffff);
	*isWrite = ((SYSTEM_CALL_ARGUMENT_3(p) >> 31) & 1);
	*targetIndex = SYSTEM_CALL_ARGUMENT_4(p);
}

uintptr_t systemCall_readWrite(int driver,
	uintptr_t buffer, uint64_t location, uintptr_t bufferSize,
	uintptr_t targetIndex, int isWrite
){
	uint32_t locationLow = (location & 0xffffffff);
	uint32_t locationHigh = ((location >> 32) & 0xffffffff);
	uintptr_t rwBufferSize = (bufferSize | (isWrite? 0x80000000: 0));
	return systemCall6(driver,
		buffer, locationLow, locationHigh,
		rwBufferSize, targetIndex
	);
}

uintptr_t systemCall_readWriteSync(int driver,
	uintptr_t buffer, uint64_t location, uintptr_t bufferSize,
	uintptr_t targetIndex, int isWrite
){
	uintptr_t ior1 = systemCall_readWrite(driver, buffer, location, bufferSize, targetIndex, isWrite);
	if(ior1 == IO_REQUEST_FAILURE)
		return ior1;
	uintptr_t ior2 = systemCall_waitIO(ior1);
	assert(ior1 == ior2);
	return ior1;
}

// MBR & EBR
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

static_assert(sizeof(struct MBR) == 512);
static_assert(sizeof(struct PartitionEntry) == 16);

typedef struct DiskPartition DiskPartition;
struct DiskPartition{
	Resource resource;
	DiskPartitionType type;
	ServiceName driverName;
	int driver;
	uintptr_t diskCode; // assigned by disk driver
	uint64_t startLBA;
	uint64_t sectorCount;
	uintptr_t sectorSize;
};
/*
struct DiskPartitionManager{
	Spinlock lock; // improve: read-write lock
	struct DiskPartition *unknownDPList;
	struct NewDiskEvent *listenDiskEvent[MAX_DISK_TYPE];
};
static struct DiskPartitionManager diskManager;
*/
void readPartitions(
	const char *driverName, int diskDriver, uintptr_t diskCode, uint64_t relativeLBA,
	uint64_t sectorCount, uintptr_t sectorSize
){
	struct MBR *buffer = systemCall_allocateHeap(sizeof(*buffer), KERNEL_NON_CACHED_PAGE);
	EXPECT(buffer != NULL);
	uintptr_t ior1 = systemCall_readWriteSync(diskDriver, (uintptr_t)buffer, relativeLBA, sectorSize, diskCode, 0);
	EXPECT(ior1 != IO_REQUEST_FAILURE);

	if(buffer->signature != MBR_SIGNATRUE){
		printk(" disk %x LBA %u is not a bootable partition\n", diskCode, relativeLBA);
	}

	int i;
	for(i = 0; i < 4; i++){
		struct PartitionEntry *pe = buffer->partition + i;
		const uint64_t lba = relativeLBA + pe->startLBA;
		//printk("type %x, lba %u, sector count %u ", pe->systemID, lba, pe->sectorCount);
		if(pe->systemID == MBR_EMPTY){
			//printk("(empty partition)\n");
			continue;
		}
		if(lba == 0 || lba <= relativeLBA || lba - relativeLBA > sectorCount){
			//printk("(invalid startLBA)\n");
			continue;
		}
		if(pe->sectorCount == 0 || pe->sectorCount > sectorCount - (lba - relativeLBA)){
			//printk("(invalid sectorCount)\n");
			continue;
		}
		//printk("\n");
		if(pe->systemID == MBR_EXTENDED){
			readPartitions(driverName, diskDriver, diskCode, lba, pe->sectorCount, sectorSize);
		}
		if(addDiskPartition(pe->systemID, driverName, diskDriver,
			lba, pe->sectorCount, sectorSize, diskCode) == 0){
			printk("warning: cannot register disk code %x to file system\n", diskCode);
		}
	}

	systemCall_releaseHeap(buffer);
	return;
	ON_ERROR;
	systemCall_releaseHeap(buffer);
	ON_ERROR;
	printk("cannot read partitions");
}

// file system interface
/*
typedef struct NewDiskEvent{
	IORequest this;
	Spinlock *lock;
	DiskPartition *newDPList;
	DiskPartition *servingDP;
	DiskPartition *discoveredDPList;
}NewDiskEvent;
*/
static int returnDiskValues(Resource *resource, uintptr_t *returnValues){
	DiskPartition *dp = resource->instance;
	returnValues[0] = dp->driver;
	returnValues[1] = (dp->startLBA & 0xffffffff);
	returnValues[2] = ((dp->startLBA >> 32) & 0xffffffff);
	returnValues[3] = dp->diskCode;
	returnValues[4] = dp->sectorSize;
	return 5;
}

static int matchDiskType(Resource *resource, const uintptr_t *arguments){
	DiskPartition *dp = resource->instance;
	if(dp->type != (DiskPartitionType)arguments[1])
		return 0;
	return 1;
}

uintptr_t systemCall_discoverDisk(DiskPartitionType diskType){
	return systemCall3(SYSCALL_DISCOVER_RESOURCE, RESOURCE_DISK_PARTITION, diskType);
}

// assume all arguments are valid
int addDiskPartition(
	DiskPartitionType diskType, const char *driverName, int diskDriver,
	uint64_t startLBA, uint64_t sectorCount, uintptr_t sectorSize,
	uintptr_t diskCode
){
	EXPECT(diskType >= 0 && diskType < MAX_DISK_TYPE);
	struct DiskPartition *NEW(dp);
	EXPECT(dp != NULL);
	initResource(&dp->resource, dp, matchDiskType, returnDiskValues);
	dp->type = diskType;
	strncpy(dp->driverName, driverName, MAX_NAME_LENGTH);
	dp->driver = diskDriver;
	dp->diskCode = diskCode;
	dp->startLBA = startLBA;
	dp->sectorCount = sectorCount;
	dp->sectorSize = sectorSize;
	addResource(RESOURCE_DISK_PARTITION, &dp->resource);
	return 1;
	// DELETE(dp);
	ON_ERROR;
	ON_ERROR;
	return 0;
}

#define MAX_FILE_SERVICE_NAME_LENGTH (8)

typedef struct FileSystem{
	Resource resource;
	int fileService;
	char name[MAX_FILE_SERVICE_NAME_LENGTH];
}FileSystem;
/*
struct FileSystemManager{
	Spinlock lock;
};
static struct FileSystemManager fileManager;
*/
static int matchFileSystemName(Resource *resource, const uintptr_t *arguments){
	FileSystem *fs = resource->instance;
	uintptr_t name32[MAX_NAME_LENGTH / sizeof(uintptr_t)] = {arguments[1], arguments[2]};
	if(((char*)name32)[0] == '\0' || // match any file system
	strncmp(fs->name, (const char*)name32, MAX_FILE_SERVICE_NAME_LENGTH) == 0)
		return 1;
	return 0;
}

static int returnFileService(Resource *resource, uintptr_t *returnValues){
	FileSystem *fs = resource->instance;
	returnValues[0] = fs->fileService;
	return 1;
}

int addFileSystem(int fileService, const char *name, size_t nameLength){
	EXPECT(nameLength <= MAX_FILE_SERVICE_NAME_LENGTH);
	FileSystem *NEW(fs);
	EXPECT(fs != NULL);
	initResource(&fs->resource, fs, matchFileSystemName, returnFileService);
	MEMSET0(fs->name);
	strncpy(fs->name, name, nameLength);
	fs->fileService = fileService;
	addResource(RESOURCE_FILE_SYSTEM, &fs->resource);
	return 1;
	//DELETE(fs);
	ON_ERROR;
	ON_ERROR;
	return 0;
}

uintptr_t systemCall_discoverFileSystem(const char* name, int nameLength){
	uintptr_t name32[MAX_NAME_LENGTH / sizeof(uintptr_t)];
	strncpy((char*)name32, name, nameLength);
	return systemCall4(SYSCALL_DISCOVER_RESOURCE, RESOURCE_FILE_SYSTEM, name32[0], name32[1]);
}

enum FileCommand{
	FILE_COMMAND_OPEN = 1,
	FILE_COMMAND_READ = 2,
	FILE_COMMAND_WRITE = 3,
	FILE_COMMAND_SEEK = 4,
	FILE_COMMAND_SIZE_OF = 5,
	FILE_COMMAND_CLOSE = 99
};

uintptr_t dispatchFileSystemCall(InterruptParam *p, FileFunctions *f){
#define NULL_OR_CALL(F) (F) == NULL? IO_REQUEST_FAILURE: (uintptr_t)(F)
	// TODO: check Address
	if(FILE_COMMAND_OPEN == SYSTEM_CALL_ARGUMENT_0(p)){
		return NULL_OR_CALL(f->open)((const char*)SYSTEM_CALL_ARGUMENT_1(p), SYSTEM_CALL_ARGUMENT_2(p));
	}
	void *arg;
	arg = f->checkHandle(SYSTEM_CALL_ARGUMENT_1(p), processorLocalTask());
	if(arg == NULL)
		return IO_REQUEST_FAILURE;
	switch(SYSTEM_CALL_ARGUMENT_0(p)){
	case FILE_COMMAND_READ:
		return NULL_OR_CALL(f->read)(arg, (uint8_t*)SYSTEM_CALL_ARGUMENT_2(p), SYSTEM_CALL_ARGUMENT_3(p));
	case FILE_COMMAND_WRITE:
		return NULL_OR_CALL(f->write)(arg, (uint8_t*)SYSTEM_CALL_ARGUMENT_2(p), SYSTEM_CALL_ARGUMENT_3(p));
	case FILE_COMMAND_SEEK:
		return NULL_OR_CALL(f->seek)(arg, SYSTEM_CALL_ARGUMENT_2(p) + (((uint64_t)SYSTEM_CALL_ARGUMENT_3(p))<<32));
	case FILE_COMMAND_SIZE_OF:
		return NULL_OR_CALL(f->sizeOf)(arg);
	case FILE_COMMAND_CLOSE:
		return NULL_OR_CALL(f->close)(arg);
	default:
		return IO_REQUEST_FAILURE;
	}
#undef NULL_OR_CALL
}


uintptr_t systemCall_openFile(int fileService, const char *fileName, uintptr_t nameLength){
	return systemCall4(fileService, FILE_COMMAND_OPEN, (uintptr_t)fileName, nameLength);
}

uintptr_t syncOpenFile(int fileService, const char *fileName, uintptr_t nameLength){
	uintptr_t handle;
	uintptr_t r = systemCall_openFile(fileService, fileName, nameLength);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, &handle))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_readFile(int fileService, uintptr_t handle, void *buffer, uintptr_t bufferSize){
	return systemCall5(fileService, FILE_COMMAND_READ, handle, (uintptr_t)buffer, bufferSize);
}

uintptr_t syncReadFile(int fileService, uintptr_t handle, void *buffer, uintptr_t *bufferSize){
	uintptr_t r;
	r = systemCall_readFile(fileService, handle, buffer, *bufferSize);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, bufferSize))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_writeFile(int fileService, uintptr_t handle, const void *buffer, uintptr_t bufferSize){
	return systemCall5(fileService, FILE_COMMAND_WRITE, handle, (uintptr_t)buffer, bufferSize);
}

uintptr_t systemCall_seekFile(int fileService, uintptr_t handle, uint64_t position){
	uintptr_t positionLow = ((position >> 0) & 0xffffffff);
	uintptr_t positionHigh = ((position >> 32) & 0xffffffff);
	return systemCall5(fileService, FILE_COMMAND_SEEK, handle, positionLow, positionHigh);
}

uintptr_t syncSeekFile(int fileService, uintptr_t handle, uint64_t position){
	uintptr_t r;
	r = systemCall_seekFile(fileService, handle, position);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIO(r))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_sizeOfFile(int fileService, uintptr_t handle){
	return systemCall3(fileService, FILE_COMMAND_SIZE_OF, handle);
}

uintptr_t systemCall_closeFile(int fileService, uintptr_t handle){
	return systemCall3(fileService, FILE_COMMAND_CLOSE, handle);
}

uintptr_t syncCloseFile(int fileService, uintptr_t handle){
	uintptr_t r;
	r = systemCall_closeFile(fileService, handle);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIO(r))
		return IO_REQUEST_FAILURE;
	return handle;
}
