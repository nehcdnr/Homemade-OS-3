#include"pic_private.h"
#include"io/io.h"
#include"common.h"
#include"memory/memory.h"
#include"assembly/assembly.h"
#include"multiprocessor/spinlock.h"

// see entry.asm
uintptr_t *initialESP = NULL;

#define KERNEL_STACK_SIZE (8192)
static void wakeupOtherProcessors(LAPIC *lapic, IOAPIC *ioapic/*, TimerEventList *timer*/){
	const uint32_t lapicID = getLAPICID(lapic);
	const int n = getNumberOfLAPIC(ioapic);
	// wake up other processors
	NEW_ARRAY(initialESP, n);
	initialESP[0] = 0x7000;
	int e = 1;
	int iter;
	for(iter = 0; iter < 3; iter++){
		int i;
		for(i = 0; i < n; i++){
			uint32_t target = getLAPICIDByIndex(ioapic, i);
			assert(target < MAX_LAPIC_ID);
			if(target == lapicID)
				continue;
			if(iter == 0){ // entry.asm
				initialESP[e] = (uintptr_t)allocateKernelMemory(KERNEL_STACK_SIZE);
				initialESP[e] += KERNEL_STACK_SIZE;
				e++;
			}
			if(iter == 1){
				interprocessorINIT(lapic, target);
			}
			if(iter == 2){
				interprocessorSTARTUP(lapic, target, 0x7000);
			}
		}
		if(iter == 1){
			int t;
			sti();
			// sleep TIMER_FREQUENCY / 5 = 250 milliseconds
			for(t = 0; t < DIV_CEIL(TIMER_FREQUENCY, 4); t++){
				hlt();
			}
			cli();
		}
	}
}

static void initMultiprocessor(
	int isBSP, InterruptTable *t,
	LAPIC *lapic, IOAPIC *ioapic
){
	static SpinlockBarrier barrier1, barrier2;
	if(isBSP){
		resetBarrier(&barrier1);
		resetBarrier(&barrier2);
		wakeupOtherProcessors(lapic, ioapic);
	}
	// synchronize
	waitAtBarrier(&barrier1, getNumberOfLAPIC(ioapic));
	if(isBSP){
		assert(t == global.idt);
		initMultiprocessorPaging(t);
	}
	waitAtBarrier(&barrier2, getNumberOfLAPIC(ioapic));
}

PIC *castAPIC(APIC *apic){
	return &apic->this;
}

PIC *createPIC(InterruptTable *t){
	if(isAPICSupported() == 0){
		PIC8259 *pic8259 = initPIC8259(t);
		initProcessorLocal(1);
		return castPIC8259(pic8259);
	}
	static IOAPIC *ioapic = NULL;
	LAPIC *lapic = initLocalAPIC(t);
	APIC *NEW(apic);
	apic->this.apic = apic;
	apic->this.endOfInterrupt = apic_endOfInterrupt;
	apic->this.setPICMask = apic_setPICMask;
	apic->this.irqToVector = apic_irqToVector;
	apic->this.interruptAllOther = apic_interruptAllOther;
	apic->lapic = lapic;
	// apic->ioapic
	if(isBSP(lapic)){
		disablePIC8259();
		ioapic = initAPIC(t);
		printk("number of processors = %d\n", getNumberOfLAPIC(ioapic));
		initProcessorLocal(MAX_LAPIC_ID); //
	}
	apic->ioapic = ioapic;
	apic->this.numberOfProcessors = getNumberOfLAPIC(ioapic);
	return castAPIC(apic);
}

void initLocalTimer(PIC *pic, InterruptTable *t, TimerEventList *timer){
	if(isAPICSupported() == 0){
		setTimer8254Frequency(TIMER_FREQUENCY);
		setTimerHandler(timer, pic->irqToVector(pic, TIMER_IRQ));
		pic->setPICMask(pic, TIMER_IRQ, 0);
		return;
	}
	if(isBSP(pic->apic->lapic)){
		setTimer8254Frequency(TIMER_FREQUENCY);
		testAndResetLAPICTimer(pic->apic->lapic, pic);
		setTimerHandler(timer, getTimerVector(pic->apic->lapic));
	}
	else{
		resetLAPICTimer(pic->apic->lapic);
		setTimerHandler(timer, getTimerVector(pic->apic->lapic));
	}
	initMultiprocessor(isBSP(pic->apic->lapic), t, pic->apic->lapic, pic->apic->ioapic); // TODO: move elsewhere
}
