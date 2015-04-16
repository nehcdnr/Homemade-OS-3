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
#include"io/bios.h"

void bspEntry(void);
void apEntry(void);

SystemGlobal global;

void bspEntry(void){
	// 1. memory
	initKernelMemory();
	// 2. printk
	initKernelConsole();
	//kprintf("available memory: %u KB\n", getUsableSize(page) / 1024);
	apEntry();
}

static void initService(PIC *pic, SystemCallTable *syscallTable){
	initPS2Driver(pic, syscallTable);
	initVideoDriver(syscallTable);
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
	PIC *pic = initPIC(global.idt);
	// 8. processorLocal
	initProcessorLocal();
	ProcessorLocal *local = getProcessorLocal();
	local->pic = pic;
	local->gdt = gdt;
	local->taskManager = taskManager;
	// 10. driver
	if(isBSP){ // TODO:
		first = 0;
		initService(local->pic, global.syscallTable);
	}
	initLocalTimer(local->pic, createTimer());
	//initMultiprocessor();
//printk("kernel memory usage: %u\n", getAllocatedSize());
printk("start accepting interrupt...\n");

	sti();
	printk("halt...\n");

	while(1){
		//printk("main %d\n", cpuid_getInitialAPICID());
		hlt();
	}
}
