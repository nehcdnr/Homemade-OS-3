#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/interrupt.h"
#include"interrupt/controller/pic.h"
#include"common.h"
#include"memory/memory.h"
#include"segment/segment.h"
#include"task/task.h"
#include"assembly/assembly.h"
#include"io/io.h"

void bspEntry(void);
void apEntry(void);

static MemoryManager *memory = NULL;
static BlockManager *block = NULL;

void bspEntry(void){
	// 1. memory
	memory = initKernelMemoryManager();
	// 2. kprintf
	initKernelConsole(memory);
	block = initKernelBlockManager(memory);
	//kprintf("available memory: %u KB\n", getUsableSize(page) / 1024);
	apEntry();
}

void apEntry(void){
	ProcessorLocal *NEW(local, memory);
	// 3. GDT
	SegmentTable *gdt = createSegmentTable(memory, 4);
	SegmentSelector *codeSegment, *dataSegment;
	setSegment0(gdt);
	codeSegment = addSegment(gdt, 0, 0xffffffff, KERNEL_CODE);
	dataSegment = addSegment(gdt, 0, 0xffffffff, KERNEL_DATA);
	// 4. task
	local->taskManager = createTaskManager(memory, block, gdt, dataSegment, getEBP());
	loadgdt(gdt, codeSegment, dataSegment);
	loadTaskRegister(local->taskManager);
	// 5. IDT
	InterruptTable *idt = initInterruptTable(memory, codeSegment, local);
	lidt(idt);
	// 6. PIC
	initPIC(memory, idt, createTimer(memory));
	//initMultiprocessor();
kprintf("kernel memory usage: %u\n", getAllocatedSize(memory));
kprintf("start accepting interrupt...\n");
	sti();
	kprintf("halt...\n");
	/*
	kprintf("start sleeping\n");
	kernelSleep(10000);
	kprintf("end sleeping\n");
	*/
	while(1){
		hlt();
	}
}
