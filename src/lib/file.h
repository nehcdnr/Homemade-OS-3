#ifndef FILE_H_INCLUDED
#define FILE_H_INCLUDED

#include"std.h"

// open

typedef union{
	uintptr_t value;
	struct{
		uintptr_t enumeration: 1;
		// uintptr_t noWrite: 1;
		// uintptr_t noRead: 1;
	};
}OpenFileMode;

#define OPEN_FILE_MODE_0 ((OpenFileMode)(uintptr_t)0)
#define OPEN_FILE_MODE_ENUMERATION ((OpenFileMOde){enumeration: 1})

// get parameter
enum FileParameter{
	// file size
	FILE_PARAM_SIZE = 0x10,
	// network MTU
	FILE_PARAM_MAX_WRITE_SIZE = 0x20,
	FILE_PARAM_MIN_READ_SIZE = 0x21,
	// MAC address, IP address
	FILE_PARAM_SOURCE_ADDRESS = 0x30,
	FILE_PARAM_DESTINATION_ADDRESS = 0x31,
	FILE_PARAM_SOURCE_PORT = 0x32,
	FILE_PARAM_DESTINATION_PORT = 0x33,
	FILE_PARAM_TRANSMIT_ETHERTYPE = 0x36,
	//FILE_PARAM_RECEIVE_ETHERTYPE = 37
	FILE_PARAM_FILE_INSTANCE = 0x50
};

// enumerate
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
uintptr_t syncGetFileParameter(uintptr_t handle, enum FileParameter paramCode, uint64_t *value);
uintptr_t syncSizeOfFile(uintptr_t handle, uint64_t *size);
uintptr_t syncMaxWriteSizeOfFile(uintptr_t handle, uintptr_t *size);
uintptr_t syncMinReadSizeOfFile(uintptr_t handle, uintptr_t *size);

uintptr_t systemCall_setFileParameter(uintptr_t handle, enum FileParameter parameterCode, uint64_t value);
uintptr_t syncSetFileParameter(uintptr_t handle, enum FileParameter parameterCode, uint64_t value);

uintptr_t systemCall_closeFile(uintptr_t handle);
uintptr_t syncCloseFile(uintptr_t handle);

#endif
