#include"file.h"
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"
#include"interrupt/systemcall.h"
#include"common.h"

static_assert(sizeof(struct MBR) == 512);
static_assert(sizeof(struct PartitionEntry) == 16);

struct FileSystemManager{
	struct FileSystem{
		enum MBR_SystemID type;
		int driver;
		uint32_t index; // assigned by disk driver
		uint64_t startLBA;
		uint64_t sectorCount;
		int fileService;
	}*array;
	int maxLength;
	int length;
	Spinlock lock;
};
static struct FileSystemManager fsManager;

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

int addDiskPartition(
	enum MBR_SystemID systemID, int diskDriver,
	uint64_t startLBA, uint64_t sectorCount,
	uint32_t partitionIndex
){
	int i;
	acquireLock(&fsManager.lock);
	for(i = 0; i < fsManager.length; i++){
		if(fsManager.array[i].driver == SERVICE_NOT_EXISTING)
			break;
	}
	if(i == fsManager.length){
		if(resizePartitionArray(&fsManager) == 0){
			releaseLock(&fsManager.lock);
			return 0;
		}
	}
	struct FileSystem *dp = fsManager.array + fsManager.length;
	fsManager.length++;
	dp->type = systemID;
	dp->driver = diskDriver;
	dp->index = partitionIndex;
	dp->startLBA = startLBA;
	dp->sectorCount = sectorCount;
	dp->fileService = SERVICE_NOT_EXISTING;
	releaseLock(&fsManager.lock);
	return 1;
}

void removeDiskPartition(int diskDriver, uint32_t partitionIndex){
	int i;
	acquireLock(&fsManager.lock);
	for(i = 0; i < fsManager.length; i++){
		if(fsManager.array[i].driver == diskDriver && fsManager.array[i].index == partitionIndex){
			fsManager.array[i].driver = SERVICE_NOT_EXISTING;
		}
	}
	for(i = fsManager.length - 1; i >= 0; i--){
		if(fsManager.array[i].driver >= 0)
			break;
	}
	fsManager.length = i + 1;
	if(resizePartitionArray(&fsManager) == 0){
		printk("warning: cannot resize FileSystem array\n");
	}
	releaseLock(&fsManager.lock);
}

void initFileSystemManager(void){
	assert(fsManager.array == NULL);
	NEW_ARRAY(fsManager.array, RESIZE_LENGTH);
	if(fsManager.array == NULL){
		panic("cannot initialize file system manager");
	}
	fsManager.maxLength = RESIZE_LENGTH;
	fsManager.length = 0;
	fsManager.lock = initialSpinlock;
}
