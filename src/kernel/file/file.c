#include"memory/memory.h"
#include"io/io.h"
#include"multiprocessor/spinlock.h"
#include"interrupt/systemcall.h"
#include"interrupt/interrupt.h"
#include"multiprocessor/processorlocal.h"
#include"common.h"
#include"file.h"
#include"task/task.h"
#include"resource/resource.h"

// disk driver

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
	uintptr_t diskCode; // assigned by disk driver
	uint64_t startLBA;
	uint64_t sectorCount;
	uintptr_t sectorSize;
};

void readPartitions(
	const char *driverName, uintptr_t fileHandle, uint64_t relativeLBA,
	uint64_t sectorCount, uintptr_t sectorSize
){
	struct MBR *buffer = systemCall_allocateHeap(sizeof(*buffer), KERNEL_NON_CACHED_PAGE);
	EXPECT(buffer != NULL);
	uintptr_t readSize = sectorSize;
	uintptr_t ior1 = syncSeekReadFile(fileHandle, buffer, relativeLBA, &readSize);
	EXPECT(ior1 != IO_REQUEST_FAILURE && sectorSize == readSize);

	if(buffer->signature != MBR_SIGNATRUE){
		printk(" disk %x LBA %u is not a bootable partition\n", fileHandle, relativeLBA);
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
			readPartitions(driverName, fileHandle, lba, pe->sectorCount, sectorSize);
		}
		if(addDiskPartition(pe->systemID, driverName,
			lba, pe->sectorCount, sectorSize, fileHandle) == 0){
			printk("warning: cannot register disk code %x to file system\n", fileHandle);
		}
	}

	systemCall_releaseHeap(buffer);
	return;
	ON_ERROR;
	systemCall_releaseHeap(buffer);
	ON_ERROR;
	printk("cannot read partitions\n");
}

// file system interface
static int returnDiskValues(Resource *resource, uintptr_t *returnValues){
	DiskPartition *dp = resource->instance;
	returnValues[0] = LOW64(dp->startLBA);
	returnValues[1] = HIGH64(dp->startLBA);
	returnValues[2] = dp->diskCode;
	returnValues[3] = dp->sectorSize;
	return 4;
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
	DiskPartitionType diskType, const char *driverName,
	uint64_t startLBA, uint64_t sectorCount, uintptr_t sectorSize,
	uintptr_t fileHandle
){
	EXPECT(diskType >= 0 && diskType < MAX_DISK_TYPE);
	DiskPartition *NEW(dp);
	EXPECT(dp != NULL);
	initResource(&dp->resource, dp, matchDiskType, returnDiskValues);
	dp->type = diskType;
	strncpy(dp->driverName, driverName, MAX_NAME_LENGTH);
	dp->diskCode = fileHandle;
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
	OpenFileFunction *openFile;
	char name[MAX_FILE_SERVICE_NAME_LENGTH];
}FileSystem;

static int matchFileSystemName(Resource *resource, const uintptr_t *arguments){
	FileSystem *fs = resource->instance;
	uintptr_t name32[MAX_FILE_SERVICE_NAME_LENGTH / sizeof(uintptr_t)] = {arguments[1], arguments[2]};
	if(((char*)name32)[0] == '\0' || // match any file system
	strncmp(fs->name, (const char*)name32, MAX_FILE_SERVICE_NAME_LENGTH) == 0)
		return 1;
	return 0;
}

static int returnFileService(Resource *resource, uintptr_t *returnValues){
	returnValues[0] = (uintptr_t)resource->instance;
	return 1;
}

int addFileSystem(OpenFileFunction openFileFunction, const char *name, size_t nameLength){
	EXPECT(nameLength <= MAX_FILE_SERVICE_NAME_LENGTH);
	FileSystem *NEW(fs);
	EXPECT(fs != NULL);
	initResource(&fs->resource, fs, matchFileSystemName, returnFileService);
	memset(fs->name, 0, sizeof(fs->name));
	strncpy(fs->name, name, nameLength);
	fs->openFile = openFileFunction;
	addResource(RESOURCE_FILE_SYSTEM, &fs->resource);
	return 1;
	//DELETE(fs);
	ON_ERROR;
	ON_ERROR;
	return 0;
}

uintptr_t systemCall_discoverFileSystem(const char* name, int nameLength){
	uintptr_t name32[MAX_FILE_SERVICE_NAME_LENGTH / sizeof(uintptr_t)];
	memset(name32, 0, sizeof(name32));
	strncpy((char*)name32, name, nameLength);
	return systemCall4(SYSCALL_DISCOVER_RESOURCE, RESOURCE_FILE_SYSTEM, name32[0], name32[1]);
}

void initOpenFileRequest(OpenFileRequest *ofr, void *instance, /*Task *task, */const FileFunctions *fileFunctions){
	ofr->instance = instance;
	ofr->handle = (uintptr_t)&ofr->handle;
	//ofr->task = task;
	ofr->fileFunctions = (*fileFunctions);
	ofr->next = NULL;
	ofr->prev = NULL;
}

// IMPROVE: openFileHashTable
struct OpenFileManager{
	OpenFileRequest *openFileList;
	Spinlock lock;
	int referenceCount;
};

OpenFileManager *createOpenFileManager(void){
	OpenFileManager *NEW(ofm);
	if(ofm == NULL){
		return NULL;
	}
	ofm->openFileList = NULL;
	ofm->lock = initialSpinlock;
	ofm->referenceCount = 0;
	return ofm;
}

void deleteOpenFileManager(OpenFileManager *ofm){
	assert(ofm->openFileList == NULL && ofm->referenceCount == 0 && isAcquirable(&ofm->lock));
	DELETE(ofm);
}

int addOpenFileManagerReference(OpenFileManager *ofm, int n){
	acquireLock(&ofm->lock);
	ofm->referenceCount += n;
	int refCnt = ofm->referenceCount;
	releaseLock(&ofm->lock);
	return refCnt;
}

static OpenFileRequest *searchOpenFileList(OpenFileManager *ofm, uintptr_t handle){
	OpenFileRequest *ofr;
	acquireLock(&ofm->lock);
	for(ofr = ofm->openFileList; ofr != NULL; ofr = ofr->next){
		if(ofr->handle == handle)
			break;
	}
	releaseLock(&ofm->lock);
	if(ofr == NULL)
		return NULL;
	return ofr;
}

void addToOpenFileList(OpenFileManager *ofm, OpenFileRequest *ofr){
	acquireLock(&ofm->lock);
	ADD_TO_DQUEUE(ofr, &ofm->openFileList);
	releaseLock(&ofm->lock);
}

void removeFromOpenFileList(OpenFileManager *ofm, OpenFileRequest *ofr){
	acquireLock(&ofm->lock);
	REMOVE_FROM_DQUEUE(ofr);
	releaseLock(&ofm->lock);
}

uintptr_t getFileHandle(OpenFileRequest *ofr){
	return ofr->handle;
}

void closeAllOpenFileRequest(OpenFileManager *ofm){
	assert(ofm->referenceCount == 0);
	while(1){
		acquireLock(&ofm->lock);
		OpenFileRequest *ofr = ofm->openFileList;
		releaseLock(&ofm->lock);
		if(ofr == NULL)
			break;
		assert(ofr->fileFunctions.close != NULL);
		uintptr_t r = syncCloseFile(ofr->handle);
		if(r == IO_REQUEST_FAILURE){
			printk("warning: cannot close file %x", ofr->handle);
			REMOVE_FROM_DQUEUE(ofr);
		}
	}
}

#define NULL_OR_CALL(F) (F) == NULL? IO_REQUEST_FAILURE: (uintptr_t)(F)
static uintptr_t dispatchFileNameCommand(FileSystem *fs, const char *fileName, uintptr_t nameLength, InterruptParam *p){
	switch(SYSTEM_CALL_NUMBER(p)){
	case SYSCALL_OPEN_FILE:
		return NULL_OR_CALL(fs->openFile)(fileName, nameLength);
	default:
		return IO_REQUEST_FAILURE;
	}
}

static uintptr_t dispatchFileHandleCommand(const InterruptParam *p){
	// TODO: check Address
	OpenFileRequest *arg = searchOpenFileList(getOpenFileManager(processorLocalTask()), SYSTEM_CALL_ARGUMENT_0(p));
	if(arg == NULL)
		arg = searchOpenFileList(globalOpenFileManager, SYSTEM_CALL_ARGUMENT_0(p));
	if(arg == NULL)
		return IO_REQUEST_FAILURE;
	const FileFunctions *f = &arg->fileFunctions;
	if(f->isValidFile != NULL && f->isValidFile(arg) == 0){
		return IO_REQUEST_FAILURE;
	}
	switch(SYSTEM_CALL_NUMBER(p)){
	case SYSCALL_CLOSE_FILE:
		return NULL_OR_CALL(f->close)(arg);
	case SYSCALL_READ_FILE:
		return NULL_OR_CALL(f->read)(arg, (uint8_t*)SYSTEM_CALL_ARGUMENT_1(p), SYSTEM_CALL_ARGUMENT_2(p));
	case SYSCALL_WRITE_FILE:
		return NULL_OR_CALL(f->write)(arg, (uint8_t*)SYSTEM_CALL_ARGUMENT_1(p), SYSTEM_CALL_ARGUMENT_2(p));
	case SYSCALL_SEEK_FILE:
		return NULL_OR_CALL(f->seek)(arg, COMBINE64(SYSTEM_CALL_ARGUMENT_1(p), SYSTEM_CALL_ARGUMENT_2(p)));
	case SYSCALL_SEEK_READ_FILE:
		return NULL_OR_CALL(f->seekRead)(arg, (uint8_t*)SYSTEM_CALL_ARGUMENT_1(p),
				COMBINE64(SYSTEM_CALL_ARGUMENT_2(p), SYSTEM_CALL_ARGUMENT_3(p)), SYSTEM_CALL_ARGUMENT_4(p));
	case SYSCALL_SEEK_WRITE_FILE:
		return NULL_OR_CALL(f->seekRead)(arg, (uint8_t*)SYSTEM_CALL_ARGUMENT_1(p),
				COMBINE64(SYSTEM_CALL_ARGUMENT_2(p), SYSTEM_CALL_ARGUMENT_3(p)), SYSTEM_CALL_ARGUMENT_4(p));
	case SYSCALL_SIZE_OF_FILE:
		return NULL_OR_CALL(f->sizeOf)(arg);
	default:
		return IO_REQUEST_FAILURE;
	}
}
#undef NULL_OR_CALL

uintptr_t systemCall_openFile(const char *fileName, uintptr_t fileNameLength){
	return systemCall3(SYSCALL_OPEN_FILE, (uintptr_t)fileName, fileNameLength);
}

uintptr_t syncOpenFile(const char *fileName){
	return syncOpenFileN(fileName, strlen(fileName));
}

uintptr_t syncOpenFileN(const char *fileName, uintptr_t nameLength){
	uintptr_t handle;
	uintptr_t r = systemCall_openFile(fileName, nameLength);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, &handle))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_closeFile(uintptr_t handle){
	return systemCall2(SYSCALL_CLOSE_FILE, handle);
}

uintptr_t syncCloseFile(uintptr_t handle){
	uintptr_t r;
	r = systemCall_closeFile(handle);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIO(r))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_readFile(uintptr_t handle, void *buffer, uintptr_t bufferSize){
	return systemCall4(SYSCALL_READ_FILE, handle, (uintptr_t)buffer, bufferSize);
}

uintptr_t syncReadFile(uintptr_t handle, void *buffer, uintptr_t *bufferSize){
	uintptr_t r;
	r = systemCall_readFile(handle, buffer, *bufferSize);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, bufferSize))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_writeFile(uintptr_t handle, const void *buffer, uintptr_t bufferSize){
	return systemCall4(SYSCALL_WRITE_FILE, handle, (uintptr_t)buffer, bufferSize);
}

uintptr_t systemCall_seekFile(uintptr_t handle, uint64_t position){
	return systemCall4(SYSCALL_SEEK_FILE, handle, LOW64(position), HIGH64(position));
}

uintptr_t systemCall_seekReadFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t bufferSize){
	return systemCall6(SYSCALL_SEEK_READ_FILE, handle, (uintptr_t)buffer,
		LOW64(position), HIGH64(position), bufferSize);
}

uintptr_t syncSeekReadFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t *bufferSize){
	uintptr_t r;
	r = systemCall_seekReadFile(handle, buffer, position, *bufferSize);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, bufferSize))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_seekWriteFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t bufferSize){
	return systemCall6(SYSCALL_SEEK_READ_FILE, handle, (uintptr_t)buffer,
		LOW64(position), HIGH64(position), bufferSize);
}

uintptr_t syncSeekFile(uintptr_t handle, uint64_t position){
	uintptr_t r;
	r = systemCall_seekFile(handle, position);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIO(r))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_sizeOfFile(uintptr_t handle){
	return systemCall2(SYSCALL_SIZE_OF_FILE, handle);
}

static void bufferToPageRange(uintptr_t bufferBegin, size_t bufferSize,
	uintptr_t *pageBegin, uintptr_t *pageOffset, size_t *pageSize){
	*pageBegin = FLOOR(bufferBegin, PAGE_SIZE);
	*pageOffset = bufferBegin - (*pageBegin);
	*pageSize = CEIL(bufferBegin + bufferSize, PAGE_SIZE) - (*pageBegin);
}

int mapBufferToKernel(const void *buffer, uintptr_t size, void **mappedPage, void **mappedBuffer){
	uintptr_t pageOffset, pageBegin;
	size_t pageSize;
	bufferToPageRange((uintptr_t)buffer, size, &pageBegin, &pageOffset, &pageSize);
	*mappedPage = checkAndMapExistingPages(
		kernelLinear, getTaskLinearMemory(processorLocalTask()),
		pageBegin, pageSize, KERNEL_PAGE, 0);
	if(*mappedPage == NULL){
		return 0;
	}
	*mappedBuffer = (void*)(pageOffset + ((uintptr_t)*mappedPage));
	return 1;
}

static void FileNameCommandHandler(InterruptParam *p){
	sti();
	const void *userFileName = (const void*)SYSTEM_CALL_ARGUMENT_0(p);
	uintptr_t nameLength = SYSTEM_CALL_ARGUMENT_1(p);
	void *mappedPage;
	void *mappedBuffer;
	int ok = mapBufferToKernel(userFileName, nameLength, &mappedPage, &mappedBuffer);
	EXPECT(ok);
	const char *fileName = (const char*)mappedBuffer;

	uintptr_t i;
	for(i = 0; i < nameLength && fileName[i] != ':'; i++);
	EXPECT(i < nameLength);
	uintptr_t r = systemCall_discoverFileSystem(fileName, i);
	EXPECT(r != IO_REQUEST_FAILURE);
	uintptr_t fileSystem;
	uintptr_t r2 = systemCall_waitIOReturn(r, 1, &fileSystem);
	assert(r == r2);
	r2 = systemCall_cancelIO(r);
	assert(r2);
	SYSTEM_CALL_RETURN_VALUE_0(p) = dispatchFileNameCommand((FileSystem*)fileSystem, fileName + i + 1, nameLength - i - 1, p);
	unmapPages(kernelLinear, mappedPage);
	return;
	ON_ERROR;
	ON_ERROR;
	unmapPages(kernelLinear, mappedPage);
	ON_ERROR;
	SYSTEM_CALL_RETURN_VALUE_0(p) = IO_REQUEST_FAILURE;
}

static void FileHandleCommandHandler(InterruptParam *p){
	sti();
	SYSTEM_CALL_RETURN_VALUE_0(p) = dispatchFileHandleCommand(p);
}

void initFile(SystemCallTable *s){
	registerSystemCall(s, SYSCALL_OPEN_FILE, FileNameCommandHandler, 0);
	registerSystemCall(s, SYSCALL_CLOSE_FILE, FileHandleCommandHandler, 1);
	registerSystemCall(s, SYSCALL_READ_FILE, FileHandleCommandHandler, 2);
	registerSystemCall(s, SYSCALL_WRITE_FILE, FileHandleCommandHandler, 3);
	registerSystemCall(s, SYSCALL_SEEK_FILE, FileHandleCommandHandler, 4);
	registerSystemCall(s, SYSCALL_SEEK_READ_FILE, FileHandleCommandHandler, 5);
	registerSystemCall(s, SYSCALL_SEEK_WRITE_FILE, FileHandleCommandHandler, 6);
	registerSystemCall(s, SYSCALL_SIZE_OF_FILE, FileHandleCommandHandler, 7);
}
