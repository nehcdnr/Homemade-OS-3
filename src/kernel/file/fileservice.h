#ifndef FILE_SERVICE_H_INCLUDED
#define FILE_SERVICE_H_INCLUDED

#include<std.h>
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"
#include"io/ioservice.h"
#include"file.h"

// disk driver

void readPartitions(const char *fileName, uintptr_t nameLength, uint64_t sectorCount, uintptr_t sectorSize);

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
	int (*setParameter)(FileIORequest2 *fior2, OpenedFile *of, uintptr_t parameterCode, uint64_t value);
	void (*close)(CloseFileRequest *cfr, OpenedFile *of);
};

// always return 0
int dummyRead(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize);
int dummyWrite(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize);
int dummySeekRead(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
int dummySeekWrite(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uint64_t position, uintptr_t bufferSize);
int dummyGetParameter(FileIORequest2 *fior2, OpenedFile *of, uintptr_t parameterCode);
int dummySetParameter(FileIORequest2 *fior2, OpenedFile *of, uintptr_t parameterCode, uint64_t value);
void dummyClose(CloseFileRequest *cfr, OpenedFile *of);

int seekReadByOffset(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize);
int seekWriteByOffset(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize);

// use macro to check number of arguments
#define INITIAL_FILE_FUNCTIONS \
	{dummyRead, dummyWrite, dummySeekRead, dummySeekWrite, dummyGetParameter, dummySetParameter, dummyClose}

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
void initKernelFile(void);

// FIFO with file system call
void initFIFOFile(void);
typedef struct FIFOFile FIFOFile;
// open/close/write are non-blocking; read is blocking
uintptr_t syncOpenFIFOFile(void);
FIFOFile *syncGetFIFOFile(uintptr_t fileHandle);
int directWriteFIFOFile(FIFOFile *fifo, const uint8_t *buffer, uintptr_t bufferSize);

#endif
