#include"task.h"
#include"segment/segment.h"
#include"memory/memory.h"
#include"memory/page.h"
#include"interrupt/handler.h"
#include"common.h"

typedef struct TaskStateSegment{
	uint16_t previousTaskLink, reserved0;
	uint32_t esp0;
	uint16_t ss0, reserved1;
	uint32_t esp1;
	uint16_t ss1, reserved2;
	uint32_t esp2;
	uint16_t ss2, reserved3;
	uint32_t cr3, eip, eflags,
	eax, ecx, edx, ebx, esp, ebp, esi, edi;
	uint16_t
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

typedef struct Task{
	enum TaskState{
		RUNNING,
		READY,
		WAITING,
		UNUSED
	}state;
	struct InterruptParam registers;
	SegmentTable *ldt;
	PageDirectory *pageTable;
}Task;
/*
static void ltr(SegmentSelector *tssSelector){
	uint16_t s = toShort(tssSelector);
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
	uint32_t kernelESP
){
	TSS *tss = allocate(m, sizeof(TSS));
	memset(tss, 0, sizeof(TSS));
	tss->ss0 = toShort(kernelSS);
	tss->esp0 = kernelESP;
	tss->ioBitmapAddress = sizeof(TSS);
	// set eflags IOPL
	// clear eflags ni
	addSegment(gdt, (uintptr_t)tss, sizeof(TSS) - 1, KERNEL_TSS);
}
