#include"memory/memory.h"
#include"io/io.h"
#include"multiprocessor/spinlock.h"
#include"interrupt/systemcall.h"
#include"multiprocessor/processorlocal.h"
#include"common.h"
#include"file.h"
#include"resource/resource.h"

// disk driver interface

uintptr_t systemCall_rwDisk(int driver,
	uintptr_t buffer, uint64_t lba, uint32_t sectorCount,
	uint32_t diskCode, int isWrite
){
	uint32_t lbaLow = (lba & 0xffffffff);
	uint32_t lbaHigh = ((lba >> 32) & 0xffffffff);
	uint32_t rwSectorCount = (sectorCount | (isWrite? 0x80000000: 0));
	return systemCall6(driver,
		&buffer, &lbaLow, &lbaHigh,
		&rwSectorCount, &diskCode
	);
}

uintptr_t systemCall_rwDiskSync(int driver,
	uintptr_t buffer, uint64_t lba, uint32_t sectorCount,
	uint32_t diskCode, int isWrite
){
	uintptr_t ior1 = systemCall_rwDisk(driver, buffer, lba, sectorCount, diskCode, isWrite);
	if(ior1 == IO_REQUEST_FAILURE)
		return ior1;
	uintptr_t ior2 = systemCall_waitIO(ior1);
	assert(ior1 == ior2);
	return ior1;
}

void rwDiskArgument(struct InterruptParam *p,
	uintptr_t *buffer, uint64_t *lba, uint32_t *sectorCount,
	uint32_t *diskCode, int *isWrite
){
	*buffer = SYSTEM_CALL_ARGUMENT_0(p);
	*lba = SYSTEM_CALL_ARGUMENT_1(p) + (((uint64_t)SYSTEM_CALL_ARGUMENT_2(p)) << 32);
	*sectorCount = (SYSTEM_CALL_ARGUMENT_3(p) & 0x7fffffff);
	*isWrite = ((SYSTEM_CALL_ARGUMENT_3(p) >> 31) & 1);
	*diskCode = SYSTEM_CALL_ARGUMENT_4(p);
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
	uint32_t diskCode; // assigned by disk driver
	uint64_t startLBA;
	uint64_t sectorCount;
	uint32_t sectorSize;
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
	const char *driverName, int diskDriver, uint32_t diskCode, uint64_t relativeLBA,
	uint64_t sectorCount, uint32_t sectorSize
){
	struct MBR *buffer = systemCall_allocateHeap(sizeof(*buffer), KERNEL_NON_CACHED_PAGE);
	EXPECT(buffer != NULL);
	uintptr_t ior1 = systemCall_rwDiskSync(diskDriver, (uintptr_t)buffer, relativeLBA, 1, diskCode, 0);
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
	uintptr_t resourceType = RESOURCE_DISK_PARTITION;
	return systemCall3(SYSCALL_DISCOVER_RESOURCE, &resourceType, &diskType);
}

// assume all arguments are valid
int addDiskPartition(
	DiskPartitionType diskType, const char *driverName, int diskDriver,
	uint64_t startLBA, uint64_t sectorCount, uint32_t sectorSize,
	uint32_t diskCode
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
/*
int removeDiskPartition(int diskDriver, uint32_t diskCode){
	struct DiskPartition *dp;
	int ok = 0;
	acquireLock(&diskManager.lock);
	for(dp = diskManager.unknownDPList; dp != NULL; dp = dp->next){
		if(dp->driver == diskDriver && dp->diskCode == diskCode){
			break;
		}
	}
	if(dp != NULL){
		ok = 1;
		REMOVE_FROM_DQUEUE(dp);
	}
	releaseLock(&diskManager.lock);
	return ok;
}
*/
#define MAX_FILE_SERVICE_NAME_LENGTH (8)

typedef struct FileSystem{
	int fileService;
	char name[MAX_FILE_SERVICE_NAME_LENGTH];
	struct FileSystem *next, **prev;
}FileSystem;
struct FileSystemManager{
	Spinlock lock;
	FileSystem *fsList;
};
static struct FileSystemManager fileManager;

static void registerFileServiceHandler(InterruptParam *p){
	int fileService = (int)SYSTEM_CALL_ARGUMENT_0(p);
	uint32_t name32[MAX_FILE_SERVICE_NAME_LENGTH / 4];
	name32[0] = SYSTEM_CALL_ARGUMENT_1(p);
	name32[1] = SYSTEM_CALL_ARGUMENT_2(p);
	// init FileSystem
	FileSystem *NEW(fs);
	EXPECT(fs != NULL);
	fs->fileService = fileService;
	strncpy(fs->name, (char*)name32, MAX_FILE_SERVICE_NAME_LENGTH);
	fs->next = NULL;
	fs->prev = NULL;
	// query/add fileManager
	acquireLock(&fileManager.lock);
	int ok = 1;
	FileSystem *i;
	for(i = fileManager.fsList; i != NULL; i = i->next){
		if(strncmp(i->name, fs->name, MAX_FILE_SERVICE_NAME_LENGTH) == 0){
			ok = 0;
			break;
		}
	}
	if(ok){
		ADD_TO_DQUEUE(fs, &fileManager.fsList);
	}
	releaseLock(&fileManager.lock);
	EXPECT(ok);
	SYSTEM_CALL_RETURN_VALUE_0(p) = 1;
	return;
	ON_ERROR;
	ON_ERROR;
	DELETE(fs);
	SYSTEM_CALL_RETURN_VALUE_0(p) = 0;
}


int systemCall_registerFileService(int fileService, const char *fileServiceName){
	uintptr_t fs = (unsigned)fileService;
	uint32_t name[MAX_FILE_SERVICE_NAME_LENGTH / 4];
	strncpy((char*)name, fileServiceName, MAX_FILE_SERVICE_NAME_LENGTH);
	return (int)systemCall4(SYSCALL_REGISTER_FILE_SYSTEM, &fs, name + 0, name + 1);
}

void initFileSystemManager(SystemCallTable *sc){
	// file
	registerSystemCall(sc, SYSCALL_REGISTER_FILE_SYSTEM, registerFileServiceHandler, 0);
	fileManager.lock = initialSpinlock;
	fileManager.fsList = NULL;
}
