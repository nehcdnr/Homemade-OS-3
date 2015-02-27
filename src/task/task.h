#include<std.h>

typedef struct MemoryManager MemoryManager;
typedef struct SegmentTable SegmentTable;
typedef struct SegmentSelector SegmentSelector;
void initTaskManager(
	MemoryManager *m,
	SegmentTable *gdt,
	SegmentSelector *kernelSS,
	uint32_t kernelESP
);
