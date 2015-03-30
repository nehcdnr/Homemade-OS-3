#include"pic_private.h"
#include"io/io.h"
#include"common.h"
#include"memory/memory.h"
#include"assembly/assembly.h"

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

PIC *castAPIC(APIC *apic){
	return &apic->this;
}

PIC *initPIC(InterruptTable *t){
	if(isAPICSupported() == 0){
		PIC8259 *pic8259 = initPIC8259(t);
		PIC *pic = castPIC8259(pic8259);
		return pic;
	}
	static IOAPIC *ioapic = NULL;
	LAPIC *lapic = initLocalAPIC(t);
	APIC *NEW(apic);
	apic->this.apic = apic;
	apic->this.endOfInterrupt = apic_endOfInterrupt;
	apic->this.setPICMask = apic_setPICMask;
	apic->this.irqToVector = apic_irqToVector;
	apic->lapic = lapic;
	// apic->ioapic
	if(isBSP(lapic)){
		disablePIC8259();
		ioapic = initAPIC(t);
		apic->ioapic = ioapic;
		printk("number of processors = %d\n", getNumberOfLAPIC(ioapic));
	}
	else{
		apic->ioapic = ioapic;
	}
	return &apic->this;
}

void initLocalTimer(PIC *pic, TimerEventList *timer){
	if(isAPICSupported() == 0){
		setTimer8254Frequency(TIMER_FREQUENCY);
		replaceTimerHandler(timer, pic->irqToVector(pic, TIMER_IRQ));
		pic->setPICMask(pic, TIMER_IRQ, 0);
		return;
	}
	if(isBSP(pic->apic->lapic)){
		testAndResetLAPICTimer(pic->apic->lapic, pic);
		replaceTimerHandler(timer, getTimerVector(pic->apic->lapic));
		wakeupOtherProcessors(pic->apic->lapic, pic->apic->ioapic, timer); // TODO: move elsewhere
	}
	else{
		resetLAPICTimer(pic->apic->lapic);
		replaceTimerHandler(timer, getTimerVector(pic->apic->lapic));
	}
}
