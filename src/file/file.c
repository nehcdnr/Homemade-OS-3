#include"file.h"
#include"memory/memory.h"
#include"io/io.h"
#include"multiprocessor/spinlock.h"
#include"interrupt/systemcall.h"
#include"multiprocessor/processorlocal.h"
#include"common.h"

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

typedef struct FileSystem FileSystem;
struct FileSystem{
	FileSystemType type;
	ServiceName driverName;
	int driver;
	uint32_t diskCode; // assigned by disk driver
	uint64_t startLBA;
	uint64_t sectorCount;
	uint32_t sectorSize;
	int fileService;

	FileSystem *next, **prev;
};

struct FileSystemManager{
	Spinlock lock; // improve: read-write lock
	struct FileSystem *unknownFSList;
	struct NewDiskEvent *listenDiskEvent[MAX_FILE_SYSTEM_TYPE];
};
static struct FileSystemManager fsManager;

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

typedef struct NewDiskEvent{
	IORequest this;
	Spinlock *lock;
	FileSystem *newFSList;
	FileSystem *servingFS;
	FileSystem *managedFSList;
}NewDiskEvent;

// assume locked
static void serveNewFS(NewDiskEvent *nde){
	FileSystem *fs = nde->newFSList;
	if(fs == NULL)
		return;
	if(nde->servingFS != NULL)
		return;
	REMOVE_FROM_DQUEUE(fs);
	ADD_TO_DQUEUE(fs, &nde->servingFS);
	nde->this.handle(&nde->this);
}

static int cancelNewDiskEvent(IORequest *ior){
	NewDiskEvent *nde = ior->ioRequest;
	FileSystem **fsTail;
	acquireLock(&fsManager.lock);
	fsTail = &fsManager.unknownFSList;
	FIND_DQUEUE_TAIL(fsTail);
	APPEND_TO_DQUEUE(&nde->newFSList, fsTail);
	FIND_DQUEUE_TAIL(fsTail);
	APPEND_TO_DQUEUE(&nde->managedFSList, fsTail);
	FIND_DQUEUE_TAIL(fsTail);
	APPEND_TO_DQUEUE(&nde->servingFS, fsTail);
	releaseLock(&fsManager.lock);
	return 1;
}

static int finishNewDiskEvent(IORequest *ior, uintptr_t *returnValues){
	NewDiskEvent *nde = ior->ioRequest;
	acquireLock(nde->lock);
	FileSystem *fs = nde->servingFS;
	returnValues[0] = fs->driver;
	returnValues[1] = (fs->startLBA & 0xffffffff);
	returnValues[2] = ((fs->startLBA >> 32) & 0xffffffff);
	returnValues[3] = fs->diskCode;
	returnValues[4] = fs->sectorSize;
	assert(fs != NULL);
	REMOVE_FROM_DQUEUE(fs);
	ADD_TO_DQUEUE(fs, &nde->managedFSList);
	putPendingIO(&nde->this);
	serveNewFS(nde);
	releaseLock(nde->lock);
	return 5;
}

static NewDiskEvent *createNewDiskEvent(Spinlock *lock){
	NewDiskEvent *NEW(nde);
	if(nde == NULL){
		return NULL;
	}
	initIORequest(&nde->this, nde,
		resumeTaskByIO, processorLocalTask(),
		cancelNewDiskEvent, finishNewDiskEvent);
	nde->lock = lock;
	nde->newFSList = NULL;
	nde->servingFS = NULL;
	nde->managedFSList = NULL;
	return nde;
}

static void discoverDiskHandler(InterruptParam *p){
	sti();
	FileSystemType fsType = SYSTEM_CALL_ARGUMENT_0(p);
	EXPECT(fsType >= 0 && fsType < MAX_FILE_SYSTEM_TYPE);

	NewDiskEvent *nde = createNewDiskEvent(&fsManager.lock);
	EXPECT(nde != NULL);
	acquireLock(&fsManager.lock);
	EXPECT(fsManager.listenDiskEvent[fsType] == NULL); // file service already exists
	fsManager.listenDiskEvent[fsType] = nde;
	putPendingIO(&nde->this);
	FileSystem *fs, *fsNext;
	for(fs = fsManager.unknownFSList; fs != NULL; fs = fsNext){
		fsNext = fs->next;
		if(fs->type != fsType)
			continue;
		REMOVE_FROM_DQUEUE(fs);
		ADD_TO_DQUEUE(fs, &nde->newFSList);
	}
	serveNewFS(nde);
	releaseLock(&fsManager.lock);
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)nde;
	return;
	ON_ERROR;
	releaseLock(&fsManager.lock);
	DELETE(nde);
	ON_ERROR;
	ON_ERROR;
	SYSTEM_CALL_RETURN_VALUE_0(p) = IO_REQUEST_FAILURE;
}

uintptr_t systemCall_discoverDisk(FileSystemType diskType){
	return systemCall2(SYSCALL_DISCOVER_DISK, &diskType);
}

/*
#define RESIZE_LENGTH (16)

static int resizePartitionArray(struct FileSystemManager *pa){
	int newLength;
	if(pa->length == pa->maxLength){ // extend
		newLength = pa->maxLength + RESIZE_LENGTH;
	}
	else{ // shrink
		newLength = CEIL(pa->length, RESIZE_LENGTH) + RESIZE_LENGTH;
		if(newLength >= pa->maxLength){
			return 1;
		}
	}
	struct FileSystem *NEW_ARRAY(array2, newLength);
	if(array2 == NULL){
		return 0;
	}
	memcpy(array2, pa->array, sizeof(*array2) * pa->length);
	DELETE(pa->array);
	pa->array = array2;
	pa->maxLength = newLength;
	return 1;
}
*/
// assume all arguments are valid
int addDiskPartition(
	FileSystemType fsType, const char *driverName, int diskDriver,
	uint64_t startLBA, uint64_t sectorCount, uint32_t sectorSize,
	uint32_t diskCode
){
	EXPECT(fsType >= 0 && fsType < MAX_FILE_SYSTEM_TYPE);
	struct FileSystem *NEW(fs);
	EXPECT(fs != NULL);
	fs->type = fsType;
	strncpy(fs->driverName, driverName, MAX_NAME_LENGTH);
	fs->driver = diskDriver;
	fs->diskCode = diskCode;
	fs->startLBA = startLBA;
	fs->sectorCount = sectorCount;
	fs->sectorSize = sectorSize;
	fs->fileService = SERVICE_NOT_EXISTING;
	acquireLock(&fsManager.lock);
	NewDiskEvent *nde = fsManager.listenDiskEvent[fsType];
	if(nde == NULL){
		ADD_TO_DQUEUE(fs, &fsManager.unknownFSList);
	}
	else{
		ADD_TO_DQUEUE(fs, &nde->newFSList);
		serveNewFS(nde);
	}
	releaseLock(&fsManager.lock);
	return 1;
	// DELETE(dp);
	ON_ERROR;
	ON_ERROR;
	return 0;
}

int removeDiskPartition(int diskDriver, uint32_t diskCode){
	struct FileSystem *fs;
	int ok = 0;
	acquireLock(&fsManager.lock);
	for(fs = fsManager.unknownFSList; fs != NULL; fs = fs->next){
		if(fs->driver == diskDriver && fs->diskCode == diskCode){
			break;
		}
	}
	if(fs != NULL){
		ok = 1;
		REMOVE_FROM_DQUEUE(fs);
	}
	releaseLock(&fsManager.lock);
	return ok;
}

void initFileSystemManager(SystemCallTable *sc){
	assert(fsManager.unknownFSList == NULL);
	fsManager.unknownFSList = NULL;
	fsManager.lock = initialSpinlock;
	registerSystemCall(sc, SYSCALL_DISCOVER_DISK, discoverDiskHandler, 0);
	int i;
	for(i = 0; i < MAX_FILE_SYSTEM_TYPE; i++){
		fsManager.listenDiskEvent[i] = NULL;
	}
}
