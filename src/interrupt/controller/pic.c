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
#define KERNEL_STACK_SIZE (8192)
static void wakeupOtherProcessors(LAPIC *lapic, IOAPIC *ioapic, TimerEventList *timer){
	const uint32_t lapicID = getLAPICID(lapic);
	const int n = getNumberOfLAPIC(ioapic);
	NEW_ARRAY(initialESP, n);
	int iter = 0;
	// initialESP[0] = 0x7000; entry.asm
	for(iter = 0; iter < 3; iter++){
		int i;
		for(i = 0; i < n; i++){
			uint32_t target = getLAPICIDByIndex(ioapic, i);
			if(target == lapicID)
				continue;
			if(iter == 0){
				initialESP[i] = (uintptr_t)allocateFixed(KERNEL_STACK_SIZE);
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

static void initIOInterrupt(PIC *pic){
	initPS2Driver(irqToVector(pic, KEYBOARD_IRQ), irqToVector(pic, MOUSE_IRQ));
	setPICMask(pic, MOUSE_IRQ, 0);
	setPICMask(pic, KEYBOARD_IRQ, 0);
}

PIC *initPIC(InterruptTable *t, TimerEventList *timer, ProcessorLocal *pl){
	if(isAPICSupported() == 0){
		PIC8259 *pic8259 = initPIC8259(t);
		PIC *pic = castPIC8259(pic8259);
		setTimer8254Frequency(TIMER_FREQUENCY);
		replaceTimerHandler(timer, irqToVector(pic, TIMER_IRQ));
		initIOInterrupt(pic);
		setPICMask(pic, TIMER_IRQ, 0);
		return pic;
	}

	LAPIC *lapic = initLocalAPIC(t, pl);
	if(isBSP(lapic)){
		disablePIC8259();
		IOAPIC *ioapic = initAPIC(t);
		PIC *pic = castAPIC(ioapic);
		printk("number of processors = %d\n", getNumberOfLAPIC(ioapic));
		testAndResetLAPICTimer(lapic, ioapic);
		replaceTimerHandler(timer, getTimerVector(lapic));
		initIOInterrupt(pic);
		wakeupOtherProcessors(lapic, ioapic, timer);
		return pic;
	}
	else{
		resetLAPICTimer(lapic);
		replaceTimerHandler(timer, getTimerVector(lapic));
		return NULL; // TODO:
	}
}
