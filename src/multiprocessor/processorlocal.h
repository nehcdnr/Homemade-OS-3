#ifndef PROCESSORLOCAL_H_
//#define PROCESSORLOCAL_H_

typedef struct ProcessorLocal{
	struct TaskManager *taskManager;
	struct InterruptTable *idt;
	// optional
	struct LAPIC *lapic;
}ProcessorLocal;

#endif /* PROCESSORLOCAL_H_ */
