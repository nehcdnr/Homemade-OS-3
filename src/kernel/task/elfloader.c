#include<std.h>
#include<common.h>
#include"memory/memory.h"
#include"memory/memory_private.h"
#include"file/file.h"
#include"io/io.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"

typedef struct{
	uint8_t magic[4]; // 0x7f, "ELF"
	uint8_t bits; // 1: 32 bit; 2: 64 bit
	uint8_t endianness; // 1: little; 2: big
	uint8_t version; // 1
	uint8_t abi; // 0
	uint8_t abiVersion; // ignored
	uint8_t pad[7]; // unused
	uint16_t type; // 1: relocatable; 2: executable; 3: shared; 4: core
	uint16_t machine; // 0x03: x86; 0x3e: x86_64
	uint32_t version2; // 1
	uint32_t entry; // 8 bytes for 64 bit
	uint32_t programHeaderOffset; // 8 bytes for 64 bit
	uint32_t sectionHeaderOffset; // 8 bytes for 64 bit
	uint32_t flags;
	uint16_t elfHeaderSize;
	uint16_t programHeaderSize;
	uint16_t programHeaderLength;
	uint16_t sectionHeaderSize;
	uint16_t sectionHeaderLength;
	uint16_t sectionNameTableIndex; // index of .shstrtab section which contains the section names
}ELFHeader32;

static_assert(sizeof(ELFHeader32) == 0x34);

typedef struct{
	uint32_t segmentType; // 0: ignored; 1: set to 0 or initial value; 2: dynamic link
	uint32_t offset;
	uint32_t memoryAddress;
	uint32_t physicalAddress; // unused
	uint32_t fileSize;
	uint32_t memorySize;
	uint32_t flags; // 1: executable; 2: writable; 4: readable
	uint32_t alignSize;
}ProgramHeader32;

static_assert(sizeof(ProgramHeader32) == 32);

static PageAttribute programHeaderToPageAttribute(const ProgramHeader32 *ph){
	if(ph->flags & 2)
		return USER_WRITABLE_PAGE;
	else
		return USER_READ_ONLY_PAGE;
}

typedef struct{
	uint32_t nameOffset; // offset of name in section name table
	uint32_t type;
	uint32_t flags;
	uint32_t address;
	uint32_t offset;
	uint32_t size;
	uint32_t symbolNameTableLink; // index of .strtab for .symtab
	uint32_t relocateInfo; // index of the section to which the relocatable section applies
	uint32_t alignSize;
	uint32_t entrySize; // size of this
}ELFSectionHeader32;

static_assert(sizeof(ELFSectionHeader32) == 40);

static int checkELF32Header(ELFHeader32 *e){
	if(e->magic[0] != 0x7f || e->magic[1] != 'E' ||
		e->magic[2] != 'L' || e->magic[3] != 'F' ||
		e->bits != 1 ||
		e->endianness != 1 ||
		e->type != 2 ||
		e->elfHeaderSize != sizeof(*e) ||
		e->programHeaderSize != sizeof(ProgramHeader32))
		return 0;
	else
		return 1;
}

struct ELFLoaderParam{
	int fileService;
	uintptr_t nameLength;
	char fileName[];
};

static void elfLoader(void *arg){
	struct ELFLoaderParam *p = arg;
	uintptr_t file;
	uintptr_t request;
	file = syncOpenFile(p->fileService, p->fileName, p->nameLength);
	EXPECT(file != IO_REQUEST_FAILURE);
	// ELFHeader32
	ELFHeader32 elfHeader32;
	uintptr_t readCount = sizeof(elfHeader32);
	request = syncReadFile(p->fileService, file, &elfHeader32, &readCount);
	EXPECT(request != IO_REQUEST_FAILURE && readCount == sizeof(elfHeader32) &&
		checkELF32Header(&elfHeader32));

	// ProgramHeader32
	request = syncSeekFile(p->fileService, file, elfHeader32.programHeaderOffset);
	EXPECT(request != IO_REQUEST_FAILURE);
	const size_t programHeaderSize = elfHeader32.programHeaderLength * sizeof(ProgramHeader32);
	ProgramHeader32 *programHeader32 = allocateKernelMemory(programHeaderSize);
	EXPECT(programHeader32 != NULL);
	readCount = programHeaderSize;
	request = syncReadFile(p->fileService, file, programHeader32, &readCount);
	EXPECT(request != IO_REQUEST_FAILURE && readCount == programHeaderSize);
	uintptr_t programBegin = 0xffffffff;
	uintptr_t programEnd = 0;
	uint16_t i;
	for(i = 0; i < elfHeader32.programHeaderLength; i++){
		const ProgramHeader32 *ph = programHeader32 + i;
		if(ph->segmentType != 1) // allocate initial value
			continue;
		if(ph->alignSize == 0 || ph->alignSize % PAGE_SIZE != 0 || ph->memoryAddress < ph->fileSize)
			break;
		const uintptr_t phBegin = FLOOR(ph->memoryAddress, ph->alignSize),
			phEnd = CEIL(ph->memoryAddress + ph->memorySize, ph->alignSize);
		// check overlap
		uint16_t j;
		for(j = 0; j < i; j++){
			const ProgramHeader32 *ph2 = programHeader32 + j;
			const uintptr_t ph2Begin = FLOOR(ph2->memoryAddress, ph2->alignSize),
				ph2End = CEIL(ph2->memoryAddress + ph2->memorySize, ph2->alignSize);
			if(!(ph2End <= phBegin || ph2Begin >= phEnd))
				break;
		}
		if(j < i)
			break;
		if(phBegin > phEnd)
			break;
		programBegin = MIN(programBegin, phBegin);
		programEnd = MAX(programEnd,  + phEnd);
	}
	// check address overflow
	EXPECT(i == elfHeader32.programHeaderLength && programBegin < programEnd);
	int ok = initUserLinearBlockManager(programBegin, programEnd);
	EXPECT(ok);
	// TaskMemoryManager
	LinearMemoryManager *taskMemory = getTaskLinearMemory(processorLocalTask());
	uintptr_t address;
	for(address = programBegin; address < programEnd; address += PAGE_SIZE){
		// is address in range?
		uint16_t j;
		for(j = 0; j < elfHeader32.programHeaderLength; j++){
			const ProgramHeader32 *ph = programHeader32 + j;
			if(ph->segmentType != 1)
				continue;
			const uintptr_t phBegin = FLOOR(ph->memoryAddress, ph->alignSize),
				phEnd = CEIL(ph->memoryAddress + ph->memorySize, ph->alignSize);
			if(address >= phBegin && address < phEnd){
				break;
			}
		}
		// not failed and in range
		if(ok && j < elfHeader32.programHeaderLength){
			ok = _mapPage_L(taskMemory->page, taskMemory->physical, (void*)address, PAGE_SIZE,
				programHeaderToPageAttribute(programHeader32 + j));
			if(ok)
				continue;
		}
		// address is not in range or failed to allocate memory
		releaseLinearBlock(taskMemory->linear, address);
	}
	EXPECT(ok);
	// fill in memory
	for(i = 0; i < elfHeader32.programHeaderLength; i++){
		const ProgramHeader32 *ph = programHeader32 + i;
		if(ph->segmentType != 1)
			continue;
		if(syncSeekFile(p->fileService, file, ph->offset) == IO_REQUEST_FAILURE)
			break;
		readCount = ph->fileSize;
		if(syncReadFile(p->fileService, file, (void*)ph->memoryAddress, &readCount) == IO_REQUEST_FAILURE)
			break;
		if(readCount != ph->fileSize)
			break;
		memset((void*)(ph->memoryAddress + ph->fileSize), 0, ph->memorySize - ph->fileSize);
	}
	EXPECT(i >= elfHeader32.programHeaderLength);
	printk("elf ok\n\n");
	DELETE(programHeader32);
	// TODO: switch to user space
((void(*)(void))elfHeader32.entry)();
assert(0);
	ON_ERROR;
	ON_ERROR;
	/* let terminateCurrentTask cleanup
	while(address != programBegin){
		checkAndReleaseLinearBlock(taskMemory, address);
		address -= PAGE_SIZE;
	}
	*/
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	DELETE(programHeader32);
	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	syncCloseFile(p->fileService, file);
	ON_ERROR;
	terminateCurrentTask();
}


Task *createUserTaskFromELF(int fileService, const char *fileName, uintptr_t nameLength, int priority){
	const size_t pSize = sizeof(struct ELFLoaderParam) + nameLength * sizeof(*fileName);
	struct ELFLoaderParam *p = allocateKernelMemory(pSize);
	if(p == NULL)
		return NULL;
	p->fileService = fileService;
	p->nameLength = nameLength;
	strncpy(p->fileName, fileName, nameLength);
	Task *t = createUserTask(elfLoader, p, pSize, priority);
	releaseKernelMemory(p);
	return t;
}
