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
	FileNameFunctions fileNameFunctions;
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

int addFileSystem(const FileNameFunctions *fileNameFunctions, const char *name, size_t nameLength){
	EXPECT(nameLength <= MAX_FILE_SERVICE_NAME_LENGTH);
	FileSystem *NEW(fs);
	EXPECT(fs != NULL);
	initResource(&fs->resource, fs, matchFileSystemName, returnFileService);
	memset(fs->name, 0, sizeof(fs->name));
	strncpy(fs->name, name, nameLength);
	fs->fileNameFunctions = *fileNameFunctions;
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

static_assert(MEMBER_OFFSET(struct FileIORequest, returnValues) == MEMBER_OFFSET(FileIORequest1, returnValues));
static_assert(MEMBER_OFFSET(struct FileIORequest, returnValues) + 4 == MEMBER_OFFSET(OpenFileRequest, returnValues));

void notSupportCancelFileIO(__attribute__((__unused__)) void *instance){
	panic("notSupportCancelFileIO");
}

static void cancelFileIORequest(void *instance){
	struct FileIORequest *r0 = instance;
	r0->cancelFileIO(r0->instance);
}

static int acceptFileIORequest(void *instance, uintptr_t *returnValue){
	struct FileIORequest *r0 = instance;
	int r = r0->returnCount, i;
	for(i = 0; i < r; i++){
		returnValue[i] = r0->returnValues[i];
	}

	r0->acceptFileIO(r0->instance);
	return r;
}

void initFileIO(
	struct FileIORequest *fior, void *instance,
	/*OpenFileRequest *ofr, */CancelFileIO *cancelFileIO, AcceptFileIO *acceptFileIO
){
	initIORequest(&fior->ior, fior, cancelFileIORequest, acceptFileIORequest);
	//fior->ofr = ofr;
	fior->systemCall = 0;
	fior->instance = instance;
	fior->cancelFileIO = cancelFileIO;
	fior->acceptFileIO = acceptFileIO;
}

void initOpenFileIO(
	OpenFileRequest *ofr, void *instance,
	/*OpenFileManager *ofm, */CancelFileIO *cancelFileIO, AcceptFileIO *acceptFileIO
){
	initFileIO(&ofr->ofior, instance, cancelFileIO, acceptFileIO);
	ofr->fileManager = getOpenFileManager(processorLocalTask());;
}

static void setLastCommand(struct FileIORequest *fior, enum SystemCall fileSystemCall){
	assert(fior->systemCall == 0);
	fior->systemCall = fileSystemCall;
}

static void completeFileIO(struct FileIORequest *fior, int returnCount, ...){
	fior->returnCount = returnCount;
	va_list va;
	va_start(va, returnCount);
	int i;
	for(i = 0; i < returnCount; i++){
		fior->returnValues[i] = va_arg(va, uintptr_t);
	}
	va_end(va);
//TODO: fior->openedFile
	completeIO(&fior->ior);
}

void completeFileIO0(FileIORequest0 *r0){
	completeFileIO(&r0->fior, 0);
}

void completeFileIO1(FileIORequest1 *r1, uintptr_t v0){
	completeFileIO(&r1->fior, 1, v0);
}

void completeFileIO2(FileIORequest2 *r2, uintptr_t v0, uintptr_t v1){
	completeFileIO(&r2->fior, 2, v0, v1);
}

void completeFileIO64(FileIORequest2 *r2, uint64_t v0){
	completeFileIO2(r2, LOW64(v0), HIGH64(v0));
}
//TODO:completeCloseFile
void completeOpenFile(OpenFileRequest *r1, OpenedFile *of){
	if(of == NULL){
		completeFileIO(&r1->ofior, 1, IO_REQUEST_FAILURE);
		return;
	}
	addToOpenFileList(r1->fileManager, of);
	completeFileIO(&r1->ofior, 1, getFileHandle(of));
}

void initOpenedFile(OpenedFile *of, void *instance, /*Task *task, */const FileFunctions *fileFunctions){
	of->instance = instance;
	of->handle = (uintptr_t)&of->handle;
	of->lock = initialSpinlock;
	of->isClosing = 0;
	of->ioCount = 0;
	//ofr->task = task;
	of->fileFunctions = (*fileFunctions);
	of->next = NULL;
	of->prev = NULL;
}

static int setFileClosing(OpenedFile *of){
	acquireLock(&of->lock);
	int ok = (of->isClosing == 0 && of->ioCount == 0);
	if(ok){
		of->isClosing = 1;
		of->ioCount++;
	}
	releaseLock(&of->lock);
	return ok;
}

static int addFileIOCount(OpenedFile *of, int v){
	acquireLock(&of->lock);
	int ok = (of->isClosing == 0);
	if(ok){
		of->ioCount += v;
	}
	releaseLock(&of->lock);
	return ok;
}

// IMPROVE: openFileHashTable
struct OpenFileManager{
	OpenedFile *openFileList;
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

// TODO: pendingIOCount
static OpenedFile *searchOpenFileList(OpenFileManager *ofm, uintptr_t handle, int isClosing){
	int ok = 0;
	acquireLock(&ofm->lock);
	OpenedFile *of;
	for(of = ofm->openFileList; of != NULL; of = of->next){
		if(of->handle == handle){
			ok = (isClosing? setFileClosing(of): addFileIOCount(of, 1));
			break;
		}
	}
	releaseLock(&ofm->lock);
	return (ok? of: NULL);
}

void addToOpenFileList(OpenFileManager *ofm, OpenedFile *of){
	acquireLock(&ofm->lock);
	ADD_TO_DQUEUE(of, &ofm->openFileList);
	releaseLock(&ofm->lock);
}

void removeFromOpenFileList(OpenFileManager *ofm, OpenedFile *of){
	assert(of->isClosing);
	acquireLock(&ofm->lock);
	REMOVE_FROM_DQUEUE(of);
	releaseLock(&ofm->lock);
}

uintptr_t getFileHandle(OpenedFile *of){
	return of->handle;
}

void closeAllOpenFileRequest(OpenFileManager *ofm){
	assert(ofm->referenceCount == 0);
	while(1){
		acquireLock(&ofm->lock);
		OpenedFile *of = ofm->openFileList;
		releaseLock(&ofm->lock);
		if(of == NULL)
			break;
		assert(of->fileFunctions.close != NULL);
		uintptr_t r = syncCloseFile(of->handle);
		if(r == IO_REQUEST_FAILURE){
			printk("warning: cannot close file %x", of->handle);
			REMOVE_FROM_DQUEUE(of);
		}
	}
}

#define _UNUSED __attribute__((__unused__))

OpenFileRequest *dummyOpen(_UNUSED const char *name, _UNUSED uintptr_t nameLength, _UNUSED OpenFileMode openMode){
	return NULL;
}
FileIORequest1 *dummyRead(_UNUSED OpenedFile *of, _UNUSED uint8_t *buffer, _UNUSED uintptr_t bufferSize){
	return NULL;
}
FileIORequest1 *dummyWrite(_UNUSED OpenedFile *of, _UNUSED const uint8_t *buffer, _UNUSED uintptr_t bufferSize){
	return NULL;
}
FileIORequest0 *dummySeek(_UNUSED OpenedFile *of, _UNUSED uint64_t position){
	return NULL;
}
FileIORequest1 *dummySeekRead(_UNUSED OpenedFile *of, _UNUSED uint8_t *buffer, _UNUSED uint64_t position, _UNUSED uintptr_t bufferSize){
	return NULL;
}
FileIORequest1 *dummySeekWrite(_UNUSED OpenedFile *of, _UNUSED const uint8_t *buffer, _UNUSED uint64_t position, _UNUSED uintptr_t bufferSize){
	return NULL;
}
FileIORequest2 *dummySizeOf(_UNUSED OpenedFile *of){
	return NULL;
}
FileIORequest0 *dummyClose(_UNUSED OpenedFile *of){
	return NULL;
}
#undef _UNUSED

#define NULL_OR(R) ((R) == NULL? (NULL): &(R)->fior)
// arg0 = str; arg1 = strLen
static IORequest *dispatchFileNameCommand(
	FileSystem *fs,
	const char *str, uintptr_t strLen, InterruptParam *p
){
	const FileNameFunctions *ff = &fs->fileNameFunctions;
	struct FileIORequest *fior;
	switch(SYSTEM_CALL_NUMBER(p)){
	case SYSCALL_OPEN_FILE:
		{
			OpenFileMode m = {value: SYSTEM_CALL_ARGUMENT_2(p)};
			OpenFileRequest *r1 = ff->open(str, strLen, m);
			if(r1 != NULL){
				fior = &r1->ofior;
			}
			else{
				fior = NULL;
			}
		}
		break;
	default:
		fior = NULL;
	}
	if(fior != NULL){
		setLastCommand(fior, SYSTEM_CALL_NUMBER(p));
		return &fior->ior;
	}
	return IO_REQUEST_FAILURE;
}

static IORequest *dispatchFileHandleCommand(const InterruptParam *p){
	const int isClosing = (SYSTEM_CALL_NUMBER(p) == SYSCALL_CLOSE_FILE);
	// TODO: check Address
	OpenFileManager *ofm = getOpenFileManager(processorLocalTask());
	OpenedFile *of = searchOpenFileList(ofm, SYSTEM_CALL_ARGUMENT_0(p), isClosing);
	// TODO: remove globalOpenFileManager
	if(of == NULL){
		ofm = globalOpenFileManager;
		of = searchOpenFileList(ofm, SYSTEM_CALL_ARGUMENT_0(p), isClosing);
	}
	if(of == NULL)
		return IO_REQUEST_FAILURE;
	const FileFunctions *f = &of->fileFunctions;
	FileIORequest0 *r0;
	FileIORequest1 *r1;
	FileIORequest2 *r2;
	struct FileIORequest *fior = NULL;
	if(isClosing){
		removeFromOpenFileList(ofm, of);
		 // close cannot fail or be cancelled unless file does not exist
		r0 = f->close(of);
		fior = NULL_OR(r0);
		assert(fior != NULL && fior->ior.cancellable == 0);
		return &fior->ior;
	}
	switch(SYSTEM_CALL_NUMBER(p)){
	case SYSCALL_CLOSE_FILE:
		panic("impossible");
		break;
	case SYSCALL_READ_FILE:
		r1 = f->read(of, (uint8_t*)SYSTEM_CALL_ARGUMENT_1(p), SYSTEM_CALL_ARGUMENT_2(p));
		fior = NULL_OR(r1);
		break;
	case SYSCALL_WRITE_FILE:
		r1 = f->write(of, (uint8_t*)SYSTEM_CALL_ARGUMENT_1(p), SYSTEM_CALL_ARGUMENT_2(p));
		fior = NULL_OR(r1);
		break;
	case SYSCALL_SEEK_FILE:
		r0 = f->seek(of, COMBINE64(SYSTEM_CALL_ARGUMENT_1(p), SYSTEM_CALL_ARGUMENT_2(p)));
		fior = NULL_OR(r0);
		break;
	case SYSCALL_SEEK_READ_FILE:
		r1 = f->seekRead(of, (uint8_t*)SYSTEM_CALL_ARGUMENT_1(p),
			COMBINE64(SYSTEM_CALL_ARGUMENT_2(p), SYSTEM_CALL_ARGUMENT_3(p)), SYSTEM_CALL_ARGUMENT_4(p));
		fior = NULL_OR(r1);
		break;
	case SYSCALL_SEEK_WRITE_FILE:
		r1 = f->seekWrite(of, (uint8_t*)SYSTEM_CALL_ARGUMENT_1(p),
			COMBINE64(SYSTEM_CALL_ARGUMENT_2(p), SYSTEM_CALL_ARGUMENT_3(p)), SYSTEM_CALL_ARGUMENT_4(p));
		fior = NULL_OR(r1);
		break;
	case SYSCALL_SIZE_OF_FILE:
		r2 = f->sizeOf(of);
		fior = NULL_OR(r2);
		break;
	default:
		fior = NULL;
	}
	if(fior == NULL){
		addFileIOCount(of, -1);
		return IO_REQUEST_FAILURE;
	}
	setLastCommand(fior, SYSTEM_CALL_NUMBER(p));
	return &fior->ior;
}

#undef NULL_OR

static_assert(sizeof(OpenFileMode) == sizeof(uintptr_t));

uintptr_t systemCall_openFile(const char *fileName, uintptr_t fileNameLength, OpenFileMode openMode){
	return systemCall4(SYSCALL_OPEN_FILE, (uintptr_t)fileName, fileNameLength, openMode.value);
}

uintptr_t syncOpenFileN(const char *fileName, uintptr_t nameLength, OpenFileMode openMode){
	uintptr_t handle;
	uintptr_t r = systemCall_openFile(fileName, nameLength, openMode);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, &handle))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t syncOpenFile(const char *fileName){
	return syncOpenFileN(fileName, strlen(fileName), OPEN_FILE_MODE_0);
}

uintptr_t syncEnumerateFile(const char * fileName){
	OpenFileMode m = OPEN_FILE_MODE_0;
	m.enumeration = 1;
	return syncOpenFileN(fileName, strlen(fileName), m);
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

uintptr_t syncSeekFile(uintptr_t handle, uint64_t position){
	uintptr_t r;
	r = systemCall_seekFile(handle, position);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIO(r))
		return IO_REQUEST_FAILURE;
	return handle;
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

uintptr_t systemCall_sizeOfFile(uintptr_t handle){
	return systemCall2(SYSCALL_SIZE_OF_FILE, handle);
}

uintptr_t syncSizeOfFile(uintptr_t handle, uint64_t *size){
	uintptr_t r, sizeLow, sizeHigh;
	r = systemCall_sizeOfFile(handle);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 2, &sizeLow, &sizeHigh))
		return IO_REQUEST_FAILURE;
	*size = COMBINE64(sizeLow, sizeHigh);
	return handle;
}

void initFileEnumeration(FileEnumeration *fileEnum, const char *name){
	fileEnum->nameLength = strlen(name);
	strncpy(fileEnum->name, name, fileEnum->nameLength);
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

PhysicalAddressArray *reserveBufferPages(void *buffer, uintptr_t bufferSize, uintptr_t *bufferOffset){
	uintptr_t pageBegin, pageSize;
	bufferToPageRange((uintptr_t)buffer, bufferSize, &pageBegin, bufferOffset, &pageSize);
	return checkAndReservePages(getTaskLinearMemory(processorLocalTask()), (void*)pageBegin, pageSize);
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
	IORequest *ior = dispatchFileNameCommand((FileSystem*)fileSystem, fileName + i + 1, nameLength - i - 1, p);
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)ior;
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
	IORequest *ior = dispatchFileHandleCommand(p);
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)ior;
}

void initFile(SystemCallTable *s){
	registerSystemCall(s, SYSCALL_OPEN_FILE, FileNameCommandHandler, -1);
	registerSystemCall(s, SYSCALL_CLOSE_FILE, FileHandleCommandHandler, 1);
	registerSystemCall(s, SYSCALL_READ_FILE, FileHandleCommandHandler, 2);
	registerSystemCall(s, SYSCALL_WRITE_FILE, FileHandleCommandHandler, 3);
	registerSystemCall(s, SYSCALL_SEEK_FILE, FileHandleCommandHandler, 4);
	registerSystemCall(s, SYSCALL_SEEK_READ_FILE, FileHandleCommandHandler, 5);
	registerSystemCall(s, SYSCALL_SEEK_WRITE_FILE, FileHandleCommandHandler, 6);
	registerSystemCall(s, SYSCALL_SIZE_OF_FILE, FileHandleCommandHandler, 7);
}
