#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/interrupt.h"
#include"interrupt/systemcall.h"
#include"interrupt/controller/pic.h"
#include"common.h"
#include"memory/memory.h"
#include"memory/segment.h"
#include"task/task.h"
#include"assembly/assembly.h"
#include"io/io.h"
#include"file/file.h"
#include"resource/resource.h"

SystemGlobal global;

static void initService(void){
	void (*services[])(void) = {
		ps2Driver,
		//vbeDriver,
		kernelConsoleService,
		pciDriver,
		ahciDriver,
		//fatDriver,
		//testMemoryTask,
		kernelFileService,
		//testKFS
	};
	unsigned int i;
	Task *t;
	for(i = 0; i < LENGTH_OF(services); i++){
		t = createUserTaskWithoutLoader(services[i], 1);
		resume(t);
	}
	/*
	for(i = 0; i < 1000; i++){
		t = createUserTask(testMemoryTask, 1);
		if(t!= NULL)
			resume(t);
		else{
			i--;
		}
	}
	*/
}

void c_entry(void);
void c_entry(void){
	static volatile int first = 1;
	int isBSP = first;
	first = 0;
	if(isBSP){
		// 1. memory
		initKernelMemory();
		// 2. printk
		initKernelConsole();
		//kprintf("available memory: %u KB\n", getUsableSize(page) / 1024);
#ifndef NDEBUG
		//testMemoryManager();
		//testMemoryManager2();
		//testMemoryManager3();
#endif
	}
	// 3. GDT
	SegmentTable *gdt = createSegmentTable();
	loadgdt(gdt);
	// 4. IDT
	if(isBSP){
		global.idt = initInterruptTable(gdt);
	}
	lidt(global.idt);
	// 5. system call & exception
	if(isBSP){
		global.syscallTable = initSystemCall(global.idt);
	}
	// 6. task
	if(isBSP){
		initTaskManagement(global.syscallTable);
	}
	TaskManager *taskManager = createTaskManager(gdt);
	// 7. PIC
	PIC *pic = createPIC(global.idt);
	// 8. processorLocal
	TimerEventList *timer = createTimer();
	setProcessorLocal(pic, gdt, taskManager, timer);
	// 9. file
	if(isBSP){
		initResourceManager(global.syscallTable);
	}
	// 10. driver
	initLocalTimer(pic, global.idt, timer);

	//printk("kernel memory usage: %u\n", getAllocatedSize());
	printk("CPU #%d is ready...\n", getMemoryMappedLAPICID());
	sti();

	if(isBSP){
		initService();
	}
	while(1){
		hlt();
	}
}
