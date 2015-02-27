#include"pic.h"
#include"pic_private.h"
#include"io/io.h"
#include"common.h"
#include"memory/memory.h"
#include"assembly/assembly.h"

InterruptVector *(*irqToVector)(PIC *pic, enum IRQ irq);
void (*setPICMask)(PIC *pic, enum IRQ irq, int setMask);

// see entry.asm
volatile uintptr_t *initialESP;
#define KERNEL_STACK_SIZE (16384)
static void wakeupOtherProcessors(MemoryManager *m, LAPIC *lapic, IOAPIC *ioapic, TimerEventList *timer){
	const uint32_t lapicID = getLAPICID(lapic);
	const int n = getNumberOfLAPIC(ioapic);
	initialESP = allocate(m, sizeof(uintptr_t) * n);
	int iter = 0;
	// initialESP[0] = 0x7000; entry.asm
	for(iter = 0; iter < 3; iter++){
		int i;
		for(i = 0; i < n; i++){
			uint32_t target = getLAPICIDByIndex(ioapic, i);
			if(target == lapicID)
				continue;
			if(iter == 0){
				initialESP[i] = (uintptr_t)allocate(m, KERNEL_STACK_SIZE);
				initialESP[i] += KERNEL_STACK_SIZE;
			}
			if(iter == 1){
				interprocessorINIT(lapic, target);
			}
			if(iter == 2){
				interprocessorSTARTUP(lapic, target, 0x7000);
			}
		}
		if(iter == 1){
			sti();
			kernelSleep(timer, 200);
			cli();
		}
	}
}

PIC *initPIC(MemoryManager *m, InterruptTable *t, TimerEventList *timer){
	if(isAPICSupported() == 0){
		PIC8259 *pic8259 = initPIC8259(m, t);
		return castPIC8259(pic8259);
	}

	LAPIC *lapic = initLocalAPIC(m, t);
	if(isBSP(lapic)){
		disablePIC8259();
		IOAPIC *ioapic = initAPIC(m, t);
		kprintf("number of processors = %d\n", getNumberOfLAPIC(ioapic));
		testAndResetLAPICTimer(lapic, ioapic);
		replaceTimerHandler(timer, getTimerVector(lapic));
		wakeupOtherProcessors(m, lapic, ioapic, timer);
		return castAPIC(ioapic);
	}
	else{
		resetLAPICTimer(lapic);
		replaceTimerHandler(timer, getTimerVector(lapic));
		return NULL; // TODO:
	}
}
