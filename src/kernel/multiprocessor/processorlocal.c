#include"assembly/assembly.h"
#include"memory/memory.h"
#include"multiprocessor/processorlocal.h"
#include"task/task.h"

typedef struct ProcessorLocal{
	struct SegmentTable *gdt;
	struct InterruptController *pic;
	struct TaskManager *taskManager;
	TimerEventList *timer;
}ProcessorLocal;

// see pic.c
ProcessorLocal *(*getProcessorLocal)(void) = NULL;

#define GET_PROCESSOR_LOCAL(TYPE, NAME, VARIABLE) \
TYPE processorLocal##NAME(void){\
	EFlags eflags = getEFlags();\
	if(eflags.bit.interrupt){\
		cli();\
	}\
	TYPE v = (VARIABLE);\
	if(eflags.bit.interrupt){\
		sti();\
	}\
	return v;\
}

GET_PROCESSOR_LOCAL(PIC*, PIC, getProcessorLocal()->pic)
GET_PROCESSOR_LOCAL(SegmentTable*, GDT, getProcessorLocal()->gdt)
GET_PROCESSOR_LOCAL(TaskManager*, TaskManager, getProcessorLocal()->taskManager)
GET_PROCESSOR_LOCAL(Task*, Task, currentTask(getProcessorLocal()->taskManager))
GET_PROCESSOR_LOCAL(TimerEventList*, Timer, getProcessorLocal()->timer)

static ProcessorLocal *lapicToProcLocal = NULL;

void setProcessorLocal(PIC *pic, SegmentTable *gdt, TaskManager *taskManager, TimerEventList *timer){
	ProcessorLocal *local = getProcessorLocal();
	local->pic = pic;
	local->gdt = gdt;
	local->taskManager = taskManager;
	local->timer = timer;
}

static ProcessorLocal *getProcessorLocalByLAPIC(void){
	assert(getEFlags().bit.interrupt == 0);
	uint32_t id = getMemoryMappedLAPICID();
	return &lapicToProcLocal[id];
}

static ProcessorLocal *getProcessorLocal0(void){
	return &lapicToProcLocal[0];
}

void initProcessorLocal(uint32_t maxProcessorCount){
	if(lapicToProcLocal == NULL){
		NEW_ARRAY(lapicToProcLocal, maxProcessorCount);
		memset(lapicToProcLocal, 0, maxProcessorCount * sizeof(lapicToProcLocal[0]));
	}
	if(maxProcessorCount == 1){
		getProcessorLocal = getProcessorLocal0;
	}
	else{
		getProcessorLocal = getProcessorLocalByLAPIC;
	}
}

