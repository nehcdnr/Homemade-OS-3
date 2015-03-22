
#include<std.h>

uintptr_t findAddressOfEBDA(void);
#define EBDA_END (0xa0000)

uint8_t checksum(const void *data, size_t size);
void *searchString(const char *string, uintptr_t beginAddress, uintptr_t endAddress);
void *searchStructure(
	size_t structureSize,
	const char *string,
	uintptr_t searchBegin,
	uintptr_t searchEnd
);

typedef struct TaskManager TaskManager;
typedef struct MemoryManager MemoryManager;

void initBIOSTask(MemoryManager *m, TaskManager *tm);
