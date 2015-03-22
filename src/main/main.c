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

static MemoryManager *memory = NULL;
static BlockManager *block = NULL;

void bspEntry(void){
	// 1. memory
	memory = initKernelMemoryManager();
	// 2. printk
	block = initKernelBlockManager(memory);
	initKernelConsole(memory);
	//kprintf("available memory: %u KB\n", getUsableSize(page) / 1024);
	apEntry();
}

void apEntry(void){
	ProcessorLocal *NEW(local, memory);
	// 3. GDT
	SegmentTable *gdt = createSegmentTable(memory);
	loadgdt(gdt);
	// 4. IDT
	local->idt = initInterruptTable(memory, gdt, local);
	lidt(local->idt);
	// 5. system call & exception
	SystemCallTable *systemCall = initSystemCall(memory, local->idt);
	// 6. task
	local->taskManager = createTaskManager(memory, systemCall, block, gdt);
	initBIOSTask(memory, local->taskManager);
	// 7. PIC
	initPIC(memory, local->idt, createTimer(memory), local);
	//initMultiprocessor();
printk("kernel memory usage: %u\n", getAllocatedSize(memory));
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
