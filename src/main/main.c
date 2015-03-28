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
	SystemCallTable *systemCall = initSystemCall(local->idt);
	// 6. task
	local->taskManager = createTaskManager(systemCall, gdt);
	initVideoTask();
	// 7. PIC
	initPIC(local->idt, createTimer(), local);
	//initMultiprocessor();
printk("kernel memory usage: %u\n", getAllocatedSize());
printk("start accepting interrupt...\n");

	sti();
	printk("halt...\n");
	/*
	kprintf("start sleeping\n");
	kernelSleep(10000);
	kprintf("end sleeping\n");
	*/
	while(1){
		//printk("main %d\n", cpuid_getInitialAPICID());
		hlt();
	}
}
