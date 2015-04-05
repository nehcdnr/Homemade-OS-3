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

static void initService(PIC *pic, SystemCallTable *syscallTable){
	initPS2Driver(pic, syscallTable);
	initVideoDriver(syscallTable);
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
	ProcessorLocal *NEW(local);
	// 3. GDT
	SegmentTable *gdt = createSegmentTable();
	loadgdt(gdt);
	// 4. IDT
	local->idt = initInterruptTable(gdt, local);
	lidt(local->idt);
	// 5. system call & exception
	SystemCallTable *syscall = initSystemCall(local->idt);
	// 6. task
	local->taskManager = createTaskManager(syscall, gdt);
	// 7. PIC
	local->pic = initPIC(local->idt);
	// 8. driver
	static int first = 1;
	if(first){
		first = 0;
		initService(local->pic, syscall);
	}
	initLocalTimer(local->pic, createTimer());
	//initMultiprocessor();
printk("kernel memory usage: %u\n", getAllocatedSize());
printk("start accepting interrupt...\n");

	sti();
	printk("halt...\n");

	while(1){
		//printk("main %d\n", cpuid_getInitialAPICID());
		hlt();
	}
}
