#ifndef PROCESSORLOCAL_H_
//#define PROCESSORLOCAL_H_

typedef struct ProcessorLocal{
	struct SegmentTable *gdt;
	struct InterruptController *pic;
	struct TaskManager *taskManager;
}ProcessorLocal;

// see pic.c
typedef ProcessorLocal *(*GetProcessorLocal)(void);
extern GetProcessorLocal getProcessorLocal;

GetProcessorLocal initProcessorLocal(void);

typedef struct SystemGlobal{
	struct InterruptTable *idt;
	struct SystemCallTable *syscallTable;
	struct PageDirectory *kernelPaging;
}SystemGlobal;

extern SystemGlobal global;

#endif /* PROCESSORLOCAL_H_ */
