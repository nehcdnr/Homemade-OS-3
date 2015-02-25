#include"task.h"
#include"segment/segment.h"
#include"memory/memory.h"
#include"memory/page.h"
#include"interrupt/handler.h"
#include"common.h"

typedef struct TaskStateSegment{
	unsigned short previousTaskLink;
	unsigned short reserved0;
	unsigned int esp0;
	unsigned short ss0, reserved1;
	unsigned int esp1;
	unsigned short ss1, reserved2;
	unsigned int esp2;
	unsigned short ss2, reserved3;
	unsigned int cr3, eip, eflags,
	eax, ecx, edx, ebx, esp, ebp, esi, edi;
	unsigned short
	es, reserved4,
	cs, reserved5,
	ss, reserved6,
	ds, reserved7,
	fs, reserved8,
	gs, reserved9,
	ldtSelector, reserved10,
	degubTrap: 1,
	reserved11: 15,
	ioBitmapAddress;
}TSS;

static_assert(sizeof(TSS) == 104);

struct Task{
	struct InterruptParam registers;
	SegmentTable *ldt;
	PageDirectory *pageTable;
};
/*
static void ltr(SegmentSelector *tssSelector){
	unsigned short s = toShort(tssSelector);
	__asm__(
	"ltr %0\n"
	:
	:"a"(s)
	);
}
*/
void initTaskManager(
	MemoryManager *m,
	SegmentTable *gdt,
	SegmentSelector *kernelSS,
	unsigned int kernelESP
){
	TSS *tss = allocate(m, sizeof(TSS));
	memset(tss, 0, sizeof(TSS));
	tss->ss0 = toShort(kernelSS);
	tss->esp0 = kernelESP;
	tss->ioBitmapAddress = sizeof(TSS);
	// set eflags IOPL
	// clear eflags ni
	addSegment(gdt, (unsigned)tss, sizeof(TSS) - 1, KERNEL_TSS);
}
