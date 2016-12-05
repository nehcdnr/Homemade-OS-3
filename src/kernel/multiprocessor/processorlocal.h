#ifndef PROCESSORLOCAL_H_
#define PROCESSORLOCAL_H_
typedef struct SegmentTable SegmentTable;
typedef struct InterruptController PIC;
typedef struct TaskManager TaskManager;
typedef struct Task Task;
typedef struct TimerEventList TimerEventList;
// processorlocal.c
struct InterruptController *processorLocalPIC(void);
SegmentTable *processorLocalGDT(void);
TaskManager *processorLocalTaskManager(void);
Task *processorLocalTask(void);
TimerEventList *processorLocalTimer(void);

void initProcessorLocal(uint32_t maxProcessorCount);
void setProcessorLocal(PIC *pic, SegmentTable *gdt, TaskManager *taskManager, TimerEventList *timer);

// see pic.c
uint32_t getMemoryMappedLAPICID(void);

typedef struct SystemGlobal{
	struct InterruptTable *idt;
	struct SystemCallTable *syscallTable;
}SystemGlobal;

extern SystemGlobal global;

#endif /* PROCESSORLOCAL_H_ */
