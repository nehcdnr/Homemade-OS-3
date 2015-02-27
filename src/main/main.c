#include"multiprocessor/spinlock.h"
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
static PageManager *page = NULL;

void bspEntry(void){
	// 1. memory
	memory = initKernelMemoryManager();
	page = initKernelPageManager(memory);
	kprintf("available memory: %u KB\n", getUsableSize(page) / 1024);
	apEntry();
}

void apEntry(void){
	// 2. GDT
	SegmentTable *gdt = createSegmentTable(memory, 4);
	SegmentSelector *codeSegment, *dataSegment;
	setSegment0(gdt);
	codeSegment = addSegment(gdt, 0, 0xffffffff, KERNEL_CODE);
	dataSegment = addSegment(gdt, 0, 0xffffffff, KERNEL_DATA);
	// 3. task
	initTaskManager(memory, gdt, dataSegment, getEBP());
	loadgdt(gdt, codeSegment, dataSegment);
	// 4. IDT
	InterruptTable *idt = initInterruptTable(memory, codeSegment);
	lidt(idt);
	// 5. PIC
	/*PIC *pic = */initPIC(memory, idt, createTimer(memory));
	//initMultiprocessor();

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
