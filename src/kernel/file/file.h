#ifndef FILE_H_INCLUDED
#define FILE_H_INCLUDED

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

void readPartitions(const char *fileName, uintptr_t nameLength, uint64_t sectorCount, uintptr_t sectorSize);

// file interface
typedef union{
	uintptr_t value;
	struct{
		uintptr_t enumeration: 1;
		uintptr_t writable: 1;
		// uintptr_t noRead: 1;
	};
}OpenFileMode;

#define OPEN_FILE_MODE_0 ((OpenFileMode)(uintptr_t)0)

enum FileParameter{
	FILE_PARAM_SIZE = 0x10,
	FILE_PARAM_WRITABLE_SIZE = 0x20
};

// if failed, return IO_REQUEST_FAILURE
uintptr_t systemCall_openFile(const char *fileName, uintptr_t fileNameLength, OpenFileMode mode);
uintptr_t syncOpenFileN(const char *fileName, uintptr_t fileNameLength, OpenFileMode mode);
uintptr_t syncOpenFile(const char *fileName);
uintptr_t syncEnumerateFileN(const char *fileName, uintptr_t nameLength);
uintptr_t syncEnumerateFile(const char * fileName);
uintptr_t systemCall_readFile(uintptr_t handle, void *buffer, uintptr_t bufferSize);
uintptr_t syncReadFile(uintptr_t handle, void *buffer, uintptr_t *bufferSize);
uintptr_t systemCall_writeFile(uintptr_t handle, const void *buffer, uintptr_t bufferSize);
uintptr_t syncWriteFile(uintptr_t handle, const void *buffer, uintptr_t *bufferSize);
uintptr_t systemCall_seekReadFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t bufferSize);
uintptr_t syncSeekReadFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t *bufferSize);
uintptr_t systemCall_seekWriteFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t bufferSize);
uintptr_t systemCall_getFileParameter(uintptr_t handle, enum FileParameter parameterCode);
uintptr_t syncSizeOfFile(uintptr_t handle, uint64_t *size);
uintptr_t syncWritableSizeOfFile(uintptr_t handle, uintptr_t *size);
uintptr_t systemCall_closeFile(uintptr_t handle);
uintptr_t syncCloseFile(uintptr_t handle);

struct DiskPartitionEnumeration{
	DiskPartitionType type;
	uint64_t startLBA;
	uint64_t sectorCount;
	uintptr_t sectorSize;
};

#define MAX_FILE_ENUM_NAME_LENGTH (64)
typedef struct FileEnumeration{
	// type, access timestamp ...
	uintptr_t  nameLength;
	char name[MAX_FILE_ENUM_NAME_LENGTH];
	union{
		struct DiskPartitionEnumeration diskPartition;
		// struct
	};
}FileEnumeration;

uintptr_t enumNextDiskPartition(uintptr_t f, DiskPartitionType t, FileEnumeration *fe);

// file service functions

void initFileEnumeration(FileEnumeration *fileEnum, const char *name, uintptr_t nameLength);

typedef struct SystemCallTable SystemCallTable;
void initFile(SystemCallTable *s);

// file IO common structure

typedef void CancelFileIO(void *arg);
typedef void AcceptFileIO(void *arg);

//CancelFileIO defaultCancelFileIO;
//AcceptFileIO defaultAcceptFileIO;

typedef struct RWFileRequest RWFileRequest;
typedef struct SeekRWFileRequest SeekRWFileRequest;
typedef struct FileIORequest2 FileIORequest2;
typedef struct OpenFileRequest OpenFileRequest;
typedef struct CloseFileRequest CloseFileRequest;

typedef struct OpenedFile OpenedFile;
typedef struct FileFunctions FileFunctions;

void setRWFileIOCancellable(RWFileRequest *rwfr, void *arg, CancelFileIO *cancelFileIO);
int setRWFileIONotCancellable(RWFileRequest *rwfr);

void completeRWFileIO(RWFileRequest *r1, uintptr_t rwByteCount, uintptr_t addOffset);
void completeFileIO0(FileIORequest2 *r0);
void completeFileIO1(FileIORequest2 *r1, uintptr_t v0);
void completeFileIO2(FileIORequest2 *r2, uintptr_t v0, uintptr_t v1);
void completeFileIO64(FileIORequest2 *r2, uint64_t v0);
void failOpenFile(OpenFileRequest *r1);
void completeOpenFile(OpenFileRequest *r1, void *instance, const FileFunctions *ff);
void completeCloseFile(CloseFileRequest* r0);

typedef struct{
	int (*open)(OpenFileRequest *ofr, const char *name, uintptr_t nameLength, OpenFileMode openMode);
}FileNameFunctions;

int dummyOpen(OpenFileRequest *ofr, const char *name, uintptr_t nameLength, OpenFileMode openMode);

#define INITIAL_FILE_NAME_FUNCTIONS {dummyOpen}

int addFileSystem(const FileNameFunctions *fileNameFunctions, const char *name, size_t nameLength);

struct FileFunctions{
	int (*read)(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize);
	int (*write)(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize);
	int (*seekRead)(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
	int (*seekWrite)(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
	int (*getParameter)(FileIORequest2 *fior2, OpenedFile *of, uintptr_t parameterCode);
	void (*close)(CloseFileRequest *cfr, OpenedFile *of);
};

// always return 0
int dummyRead(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize);
int dummyWrite(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize);
int dummySeekRead(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
int dummySeekWrite(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
int dummyGetParameter(FileIORequest2 *fior2, OpenedFile *of, uintptr_t parameterCode);
void dummyClose(CloseFileRequest *cfr, OpenedFile *of);

int seekReadByOffset(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize);
int seekWriteByOffset(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize);

// use macro to check number of arguments
#define INITIAL_FILE_FUNCTIONS \
	{dummyRead, dummyWrite, dummySeekRead, dummySeekWrite, dummyGetParameter, dummyClose}

typedef struct OpenFileManager OpenFileManager;

OpenFileManager *createOpenFileManager(void);
void deleteOpenFileManager(OpenFileManager *ofm);
int addOpenFileManagerReference(OpenFileManager *ofm, int n);
void addToOpenFileList(OpenFileManager *ofm, OpenedFile *ofr);
void removeFromOpenFileList(OpenedFile *of);
uintptr_t getFileHandle(OpenedFile *of);
void *getFileInstance(OpenedFile *of);
uint64_t getFileOffset(OpenedFile *of);
// assume no pending IO requests
void closeAllOpenFileRequest(OpenFileManager *ofm);

// FAT32
void fatService(void);

// kernel file
void kernelFileService(void);

#endif
