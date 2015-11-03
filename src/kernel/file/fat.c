#include"common.h"
#include"memory/memory.h"
#include"io/io.h"
#include"file.h"
#include"interrupt/systemcall.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"multiprocessor/spinlock.h"

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
}FAT12BootRecord;

typedef struct __attribute((__packed__)){
	uint32_t sectorsPerFAT32;
	uint16_t flags; // bit 0~4: active FAT copies; bit 7: FAT mirroring disabled
	uint16_t version;
	uint32_t rootCluster; // first data cluster. typically = 2
	uint16_t fsInfoSector;
	uint16_t backupBootSector;
	uint8_t reserved[12];
	FATExtBootRecord ext;
}FAT32BootRecord;

typedef struct __attribute__((__packed__)){
	struct __attribute__((__packed__)){
		uint8_t jmp[3];
		uint8_t oemName[8];
		uint16_t bytesPerSector;
		uint8_t sectorsPerCluster;
		uint16_t reservedSectorCount;
		uint8_t fatCount;
		uint16_t rootEntryCount;
		uint16_t sectorCount;
		uint8_t mediaType;
		uint16_t sectorsPerFAT;
		uint16_t sectorsPerTrack;
		uint16_t headCount;
		uint32_t hiddenSectorCount;
		uint32_t SectorCount2;
	};
	union{
		struct{
			FAT12BootRecord ebr12;
			uint8_t fat12bootCode[448];
		};
		struct{
			FAT32BootRecord ebr32;
			uint8_t fat32BootCode[420];
		};
	};
	uint16_t bootSignature; // 0xaa55
}FATBootSector;

static_assert(sizeof(FATBootSector) == 512);

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
	uint8_t attribute;
	uint8_t type; // 0
	uint8_t checksum;
	uint16_t fileName5[6];
	uint16_t reserved; // 0
	uint16_t fileName11[2];
}LongFATDirEntry;

enum FATAttribute{
	FAT_READ_ONLY = 0x01,
	FAT_HIDDEN = 0x02,
	FAT_SYSTEM = 0x04,
	FAT_VOLUME_ID = 0x08,
	FAT_DIRECTORY = 0x10,
	FAT_ARCHIVE = 0x20,
	FAT_LONG_FILE_NAME = 0x0f
};

static_assert(sizeof(LongFATDirEntry) == 32);

#pragma pack()

//static struct SlabManager *slab = NULL;

typedef struct FAT32DiskPartition{
	uintptr_t diskFileHandle;
	uint64_t startLBA;
	uint64_t firstDataLBA;
	uintptr_t sectorSize;
	char partitionName;
	const FATBootSector *bootRecord;
	uint32_t *fat;

	struct FAT32DiskPartition **prev, *next;
}FAT32DiskPartition;

static uint32_t *loadFAT32(const FATBootSector *br, const FAT32DiskPartition *dp){
	const size_t fatSize = br->ebr32.sectorsPerFAT32 * br->bytesPerSector;
	const uint64_t fatBeginLBA = dp->startLBA + br->reservedSectorCount;
	const uintptr_t sectorsPerPage = PAGE_SIZE / dp->sectorSize;
	uint32_t *fat = systemCall_allocateHeap(CEIL(fatSize, PAGE_SIZE), KERNEL_NON_CACHED_PAGE);//TODO:
	EXPECT(fat != NULL);
	unsigned p;
	for(p = 0; p * PAGE_SIZE < fatSize; p++){
		uintptr_t readSize = PAGE_SIZE;
		uintptr_t rwDisk = syncSeekReadFile(dp->diskFileHandle,
			(void*)(((uintptr_t)fat) + PAGE_SIZE * p), fatBeginLBA + p * sectorsPerPage, &readSize);
		if(rwDisk == IO_REQUEST_FAILURE || readSize != PAGE_SIZE){
			printk("warning: failed to read FAT\n"); //TODO
			break;
		}
	}
	EXPECT(p * PAGE_SIZE >= fatSize);
	return fat;
	ON_ERROR;
	systemCall_releaseHeap(fat);
	ON_ERROR;
	return NULL;
}

static void iterateDirectory(uint32_t beginClusterSector,
	uint32_t sectorsPerCluster, const FAT32DiskPartition *dp){
	const uintptr_t clusterSize = sectorsPerCluster * dp->sectorSize;
	FATDirEntry *rootDir = systemCall_allocateHeap(clusterSize, KERNEL_NON_CACHED_PAGE);
	EXPECT(rootDir != NULL);printk("m0 %x\n",rootDir);
	MEMSET0(rootDir);printk("m0 %x\n",rootDir);
	uintptr_t readSize = clusterSize;
	uintptr_t rwDisk = syncSeekReadFile(dp->diskFileHandle,
		rootDir, beginClusterSector, &readSize);
	EXPECT(rwDisk != IO_REQUEST_FAILURE && readSize == clusterSize);
	unsigned p;
	for(p = 0; p < clusterSize / sizeof(FATDirEntry); p++){
		if(rootDir[p].fileName[0] == 0)break;
		if(rootDir[p].fileName[0] == 0xe5)continue;
		int b;
		for(b = 0; b < 11; b++){
			printk("%c", rootDir[p].fileName[b]);
		}
		if(rootDir[p].attribute & FAT_DIRECTORY)
			printk(" (dir)");
		printk(" cluster %d %d size %d", rootDir[p].clusterHigh, rootDir[p].clusterLow, rootDir[p].fileSize);
		printk("\n");
	}
	systemCall_releaseHeap(rootDir);
	return;
	ON_ERROR;
	systemCall_releaseHeap(rootDir);
	ON_ERROR;
	printk("cannot iterate directory\n");
	return;
};

static uint64_t clusterToLBA(const FAT32DiskPartition *dp, uint32_t cluster){
	return dp->firstDataLBA + (cluster - 2) * (uint64_t)dp->bootRecord->sectorsPerCluster;
}

static FAT32DiskPartition *createFATPartition(uintptr_t fileHandle, uint64_t startLBA, uintptr_t sectorSize, char partitionName){
	FAT32DiskPartition *NEW(dp);
	EXPECT(dp != NULL);
	dp->diskFileHandle = fileHandle;
	dp->startLBA = startLBA;
	dp->sectorSize = sectorSize;
	dp->partitionName = partitionName;
	const uintptr_t readSize = CEIL(sizeof(FATBootSector), dp->sectorSize);
	FATBootSector *br = systemCall_allocateHeap(readSize, KERNEL_NON_CACHED_PAGE);//TODO:
	EXPECT(br != NULL);
	dp->bootRecord = br;
	MEMSET0(br);
	uintptr_t actualReadSize = readSize;
	uintptr_t rwDisk = syncSeekReadFile(dp->diskFileHandle,
		br, dp->startLBA, &actualReadSize);
	EXPECT(rwDisk != IO_REQUEST_FAILURE && actualReadSize == readSize);
	EXPECT(br->ebr32.ext.signature == 0x28 || br->ebr32.ext.signature == 0x29);

	//const uint32_t sectorsPerFAT32 = br->ebr32.sectorsPerFAT32;
	//uint32_t bytesPerCluster = br->sectorsPerCluster * br->bytesPerSector;
	//printk("%x %x %x %x\n",br->ebr32.rootCluster, fatBeginLBA, sectorsPerFAT32, bytesPerCluster);

	uint32_t *fat = loadFAT32(br, dp);
	EXPECT(fat != NULL);
	dp->fat = fat;
	//const unsigned fatEntryCount = br->ebr32.sectorsPerFAT32 * (sectorSize / 4);
	//unsigned p;
	dp->firstDataLBA = dp->startLBA +
		(uint64_t)br->reservedSectorCount + br->ebr32.sectorsPerFAT32 * (uint64_t)br->fatCount;
	printk("read fat ok\n");
	dp->prev = NULL;
	dp->next = NULL;
	return dp;
	//systemCall_releaseHeap(fat);
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	systemCall_releaseHeap((void*)br);
	ON_ERROR;
	ON_ERROR;
	printk("warning: read FAT32 failed\n");
	return NULL;
}

#define FAT32_SERVICE_NAME "fat32"

struct FAT32Collection{
	Task *mainTask;
	FAT32DiskPartition *head;
	Spinlock lock;
}fat32List = {NULL, NULL, INITIAL_SPINLOCK};

static FAT32DiskPartition *searchFAT32DiskPartition(const char *fileName, uintptr_t nameLength){
	EXPECT(nameLength >= 2 && fileName[1] == '/');
	FAT32DiskPartition *f;
	acquireLock(&fat32List.lock);
	for(f = fat32List.head; f != NULL; f = f->next){
		if(f->partitionName == fileName[0])
			break;
	}
	releaseLock(&fat32List.lock);
	return f;
	ON_ERROR;
	return NULL;
}

static void addFAT32DiskPartition(FAT32DiskPartition *dp){
	acquireLock(&fat32List.lock);
	ADD_TO_DQUEUE(dp, &fat32List.head);
	releaseLock(&fat32List.lock);
}

typedef struct{
	OpenFileRequest ofr;
}FATFile;

typedef struct{
	IORequest ior;
	uintptr_t nameLength;
	uintptr_t fileHandle;
	char fileName[];
}OpenFATRequest;

static void cancelOpenFAT(__attribute__((__unused__)) IORequest *ior){
	// OpenFATRequest *ofr = ior->instance;
	// TODO: not supported
}

static int finishOpenFAT(IORequest *ior, uintptr_t *returnValues){
	OpenFATRequest *ofr = ior->instance;
	returnValues[0] = ofr->fileHandle;
	DELETE(ofr);
	return 1;
}

static void openFATTask(void *p);

static IORequest *openFAT(const char *fileName, uintptr_t nameLength){
	OpenFATRequest *ofp = allocateKernelMemory(sizeof(*ofp) + nameLength);
	EXPECT(ofp != NULL);
	initIORequest(&ofp->ior, ofp, cancelOpenFAT, finishOpenFAT);
	strncpy(ofp->fileName, fileName, nameLength);
	ofp->nameLength = nameLength;
	ofp->fileHandle = IO_REQUEST_FAILURE;
	Task *t = createSharedMemoryTask(openFATTask, &ofp, sizeof(ofp), fat32List.mainTask);
	EXPECT(t != NULL);
	resume(t);
	setCancellable(&ofp->ior, 0);
	pendIO(&ofp->ior);
	return &ofp->ior;
	// delete task
	ON_ERROR;
	ON_ERROR;
	return IO_REQUEST_FAILURE;
}

static IORequest *closeFAT(__attribute__((__unused__)) OpenFileRequest *ofr){
	// TODO:
	return IO_REQUEST_FAILURE;
}

static void openFATTask(void *p){
	OpenFATRequest *ofp = *(OpenFATRequest**)p;
	FAT32DiskPartition *dp = searchFAT32DiskPartition(ofp->fileName, ofp->nameLength);
	EXPECT(dp != NULL);
	FileFunctions ff = INITIAL_FILE_FUNCTIONS(openFAT, NULL, NULL, NULL, NULL, NULL, NULL, closeFAT, NULL);
	FATFile *NEW(file);
	EXPECT(file != NULL);
	initOpenFileRequest(&file->ofr, file, &ff);
	// TODO: file path
	iterateDirectory(clusterToLBA(dp, dp->bootRecord->ebr32.rootCluster), dp->bootRecord->sectorsPerCluster, dp);

	ofp->fileHandle = getFileHandle(&file->ofr);
	finishIO(&ofp->ior);
	terminateCurrentTask();
	//DELETE(file);
	ON_ERROR;
	ON_ERROR;
	ofp->fileHandle = IO_REQUEST_FAILURE;
	finishIO(&ofp->ior);
	terminateCurrentTask();
}

void fatService(void){
	fat32List.mainTask = processorLocalTask();
	//slab = createUserSlabManager();
	uintptr_t discoverFAT = systemCall_discoverDisk(MBR_FAT32);
	assert(discoverFAT != IO_REQUEST_FAILURE);
	//int diskDriver;
	uintptr_t startLBALow;
	uintptr_t startLBAHigh;
	//uintptr_t diskCode;
	//uintptr_t sectorSize;
	if(addFileSystem(openFAT, "fat", strlen("fat")) != 1){
		printk("add file system failure\n");
	}
	int i;
	for(i = 'C'; i < 'Z'; i++){
		uintptr_t diskFileHandle, sectorSize;
		uintptr_t discoverFAT2 = systemCall_waitIOReturn(
			discoverFAT, 4,
			&startLBALow, &startLBAHigh, &diskFileHandle, &sectorSize);
		if(discoverFAT != discoverFAT2){
			printk("discover disk failure\n");
			continue;
		}
		FAT32DiskPartition *dp = createFATPartition(diskFileHandle, COMBINE64(startLBALow, startLBAHigh), sectorSize, i);
		if(dp == NULL){
			continue;
		}
		addFAT32DiskPartition(dp);
		//TODO:
	}
	printk("too many fat systems\n");
	while(1){
		sleep(1000);
	}
	panic("cannot initialize FAT32 service");
}

#ifndef NDEBUG
void testFAT(void);
void testFAT(void){
	int a;
	for(a = 0; a < 4; a++){
		sleep(1500);
		printk("test open fat...\n");
		uintptr_t fileHandle = syncOpenFile("fat:C/");
		if(fileHandle != IO_REQUEST_FAILURE)
			break;
		printk("test open fat failed...\n");
	}
	printk("test open fat ok...\n");
	while(1){
		sleep(2000);
	}
}
#endif
