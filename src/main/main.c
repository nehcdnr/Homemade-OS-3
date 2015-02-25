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
static SegmentTable *gdt = NULL;
static SegmentSelector *codeSegment = NULL, *dataSegment = NULL;
static InterruptTable *idt = NULL;

void bspEntry(void){
	// 1. memory
	memory = initKernelMemoryManager();
	page = initKernelPageManager(memory);
	printf("available memory: %u KB\n", getUsableSize(page) / 1024);
	// 2. GDT
	gdt = createSegmentTable(memory, 4);
	setSegment0(gdt);
	codeSegment = addSegment(gdt, 0, 0xffffffff, KERNEL_CODE);
	dataSegment = addSegment(gdt, 0, 0xffffffff, KERNEL_DATA);
	// 3. task
	initTaskManager(memory, gdt, dataSegment, getEBP());
	loadgdt(gdt, codeSegment, dataSegment);
	// 4. IDT
	idt = initInterruptTable(memory, codeSegment);
	lidt(idt);
	// 5. PIC
	initPIC(memory, idt);
	initMultiprocessor();

printf("start accepting interrupt...\n");
	sti();
	printf("halt...\n");
	while(1){
		hlt();
	}
}

void apEntry(void){
	initTaskManager(memory, gdt, dataSegment, getEBP());
	loadgdt(gdt, codeSegment, dataSegment);
	lidt(idt);

printf("ap ok\n");

	hlt();
}
