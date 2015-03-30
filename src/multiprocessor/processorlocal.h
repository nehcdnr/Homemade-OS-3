#ifndef PROCESSORLOCAL_H_
//#define PROCESSORLOCAL_H_

typedef struct ProcessorLocal{
	struct TaskManager *taskManager;
	struct InterruptTable *idt;
	struct InterruptController *pic;
}ProcessorLocal;

#endif /* PROCESSORLOCAL_H_ */
