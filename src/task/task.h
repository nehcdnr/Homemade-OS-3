#include<std.h>

typedef struct MemoryManager MemoryManager;
typedef struct BlockManager BlockManager;
typedef struct SegmentTable SegmentTable;
typedef struct SegmentSelector SegmentSelector;
typedef struct TaskManager TaskManager;

typedef struct InterruptParam InterruptParam;
void schedule(TaskManager *tm, InterruptParam *p);

TaskManager *createTaskManager(
	MemoryManager *m,
	BlockManager *b,
	SegmentTable *gdt,
	SegmentSelector *kernelSS,
	uint32_t kernelESP
);

void loadTaskRegister(TaskManager *tm);
