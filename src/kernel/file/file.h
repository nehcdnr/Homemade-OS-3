#include<std.h>
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"
#include"io/io.h"

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
uintptr_t systemCall_discoverFileSystem(const char* name, int nameLength);

// file interface
typedef union{
	uintptr_t value;
	struct{
		uintptr_t enumeration: 1;
		uintptr_t writable: 1;
	};
}OpenFileMode;

#define OPEN_FILE_MODE_0 ((OpenFileMode)(uintptr_t)0)

// if failed, return IO_REQUEST_FAILURE
uintptr_t systemCall_openFile(const char *fileName, uintptr_t fileNameLength, OpenFileMode mode);
uintptr_t syncOpenFileN(const char *fileName, uintptr_t fileNameLength, OpenFileMode mode);
uintptr_t syncOpenFile(const char *fileName);
uintptr_t syncEnumerateFile(const char * fileName);
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

#define MAX_FILE_ENUM_NAME_LENGTH (64)
typedef struct FileEnumeration{
	// type, access timestamp ...
	uintptr_t  nameLength;
	char name[MAX_FILE_ENUM_NAME_LENGTH];
}FileEnumeration;

// file service functions

void initFileEnumeration(FileEnumeration *fileEnum, const char *name);

// call unmapPages(kernelLinear, mappedPage) to release
int mapBufferToKernel(const void *buffer, uintptr_t size, void **mappedPage, void **mappedBuffer);
PhysicalAddressArray *reserveBufferPages(void *buffer, uintptr_t bufferSize, uintptr_t *bufferOffset);

typedef struct SystemCallTable SystemCallTable;
void initFile(SystemCallTable *s);

// file IO common structure

typedef void CancelFileIO(void *instance);
typedef void AcceptFileIO(void *instance);

void notSupportCancelFileIO(void *instance);

struct FileIORequest{
	IORequest ior;
	void *instance;
	// OpenFileRequest *ofr;
	uintptr_t systemCall;
	CancelFileIO *cancelFileIO;
	AcceptFileIO *acceptFileIO;
	int returnCount;
	uintptr_t returnValues[0];
};

typedef struct{
	struct FileIORequest fior;
	//uintptr_t returnValues[0];
}FileIORequest0;

typedef struct{
	struct FileIORequest fior;
	uintptr_t returnValues[1];
}FileIORequest1;

typedef struct{
	struct FileIORequest fior;
	uintptr_t returnValues[2];
}FileIORequest2;

typedef struct{
	struct OpenFileManager *fileManager;
	struct FileIORequest ofior;
	uintptr_t returnValues[1];
}OpenFileRequest;

typedef struct OpenedFile OpenedFile;

typedef struct{
	OpenedFile *file;
	struct FileIORequest cfior;
	//uintptr_t returnValues[0];
}CloseFileRequest;

void initFileIO(
	struct FileIORequest *r0, void *instance,
	/*OpenFileRequest *ofr, */CancelFileIO *cancelFileIO, AcceptFileIO *acceptFileIO
);

#define INIT_FILE_IO(FIOR, INSTANCE, CANCEL, ACCEPT) \
	initFileIO(&((FIOR)->fior), (INSTANCE), (CANCEL), (ACCEPT))

void initOpenFileIO(
	OpenFileRequest *ofr, void *instance,
	CancelFileIO *cancelFileIO, AcceptFileIO *acceptFileIO
);

CloseFileRequest *setCloseFileIO(OpenedFile *of, void *instance, AcceptFileIO *acceptFileIO);

void completeFileIO0(FileIORequest0 *r0);
void completeFileIO1(FileIORequest1 *r1, uintptr_t v0);
void completeFileIO2(FileIORequest2 *r2, uintptr_t v0, uintptr_t v1);
void completeFileIO64(FileIORequest2 *r2, uint64_t v0);
void completeOpenFile(OpenFileRequest *r1, OpenedFile *of);
void completeCloseFile(CloseFileRequest* r0);

typedef struct{
	OpenFileRequest *(*open)(const char *name, uintptr_t nameLength, OpenFileMode openMode);
}FileNameFunctions;

OpenFileRequest *dummyOpen(const char *name, uintptr_t nameLength, OpenFileMode openMode);

#define INITIAL_FILE_NAME_FUNCTIONS {dummyOpen}

int addFileSystem(const FileNameFunctions *fileNameFunctions, const char *name, size_t nameLength);

typedef struct{
	FileIORequest1 *(*read)(OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize);
	FileIORequest1 *(*write)(OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize);
	FileIORequest0 *(*seek)(OpenedFile *of, uint64_t position/*, whence*/);
	FileIORequest1 *(*seekRead)(OpenedFile *of, uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
	FileIORequest1 *(*seekWrite)(OpenedFile *of, const uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
	FileIORequest2 *(*sizeOf)(OpenedFile *of);
	CloseFileRequest *(*close)(OpenedFile *of);
}FileFunctions;

// always return NULL
FileIORequest1 *dummyRead(OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize);
FileIORequest1 *dummyWrite(OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize);
FileIORequest0 *dummySeek(OpenedFile *of, uint64_t position);
FileIORequest1 *dummySeekRead(OpenedFile *of, uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
FileIORequest1 *dummySeekWrite(OpenedFile *of, const uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
FileIORequest2 *dummySizeOf(OpenedFile *of);
CloseFileRequest *dummyClose(OpenedFile *of);

// use macro to check number of arguments
#define INITIAL_FILE_FUNCTIONS \
	{dummyRead, dummyWrite, dummySeek, dummySeekRead, dummySeekWrite, dummySizeOf, dummyClose}

typedef struct OpenFileManager OpenFileManager;

struct OpenedFile{
	void *instance;
	uintptr_t handle;
	FileFunctions fileFunctions;

	Spinlock lock;
	uint32_t isClosing; // atomic read/write 32
	uint32_t ioCount;
	CloseFileRequest cfr;
	OpenFileManager *fileManager;
	//Task *task;
	OpenedFile *next, **prev;
};

void initOpenedFile(
	OpenedFile *of,
	void *instance,
	//Task *task,
	const FileFunctions *fileFunctions
);

OpenFileManager *createOpenFileManager(void);
void deleteOpenFileManager(OpenFileManager *ofm);
int addOpenFileManagerReference(OpenFileManager *ofm, int n);
void addToOpenFileList(OpenFileManager *ofm, OpenedFile *ofr);
void removeFromOpenFileList(OpenedFile *of);
uintptr_t getFileHandle(OpenedFile *of);
// assume no pending IO requests
void closeAllOpenFileRequest(OpenFileManager *ofm);

extern OpenFileManager *globalOpenFileManager; // see taskmanager.c

// FAT32
void fatService(void);

// kernel file
void kernelFileService(void);
