#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/interrupt.h"
#include"interrupt/systemcall.h"
#include"interrupt/controller/pic.h"
#include"common.h"
#include"memory/memory.h"
#include"segment/segment.h"
#include"task/task.h"
#include"assembly/assembly.h"
#include"io/io.h"
#include"file/file.h"

void bspEntry(void);
void apEntry(void);

SystemGlobal global;

static void initService(void){
	void (*services[])(void) = {
		ps2Driver,/* vbeDriver, */kernelConsoleService, pciDriver, ahciDriver, fatDriver
	};
	unsigned int i;
	Task *t;
	for(i = 0; i < LENGTH_OF(services); i++){
		t = createKernelTask(services[i], 0);
		resume(t);
	}
}

void bspEntry(void){
	// 1. memory
	initKernelMemory();
	// 2. printk
	initKernelConsole();
	//kprintf("available memory: %u KB\n", getUsableSize(page) / 1024);
	apEntry();
}

void apEntry(void){
	static volatile int first = 1;
	int isBSP = first;
	first = 0;
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
		initFileSystemManager(global.syscallTable);
	}
	// 10. driver
	initLocalTimer(pic, global.idt, timer);

	//printk("kernel memory usage: %u\n", getAllocatedSize());
	printk("start accepting interrupt...\n");
	sti();

	if(isBSP){
		initService();
	}
	while(1){
		hlt();
	}
}
