#include"pic_private.h"
#include"io/io.h"
#include"common.h"
#include"memory/memory.h"
#include"assembly/assembly.h"
#include"multiprocessor/spinlock.h"

// see entry.asm
uintptr_t *initialESP = NULL;

typedef struct SpinlockBarrier{
	Spinlock lock;
	volatile int count;
}SpinlockBarrier;

static void resetBarrier(SpinlockBarrier *b){
	b->lock = initialSpinlock;
	b->count = 0;
}

static void syncBarrier(SpinlockBarrier *b, int threadCount){
	acquireLock(&b->lock);
	b->count++;
	releaseLock(&b->lock);
	int continueFlag = 1;
	while(continueFlag){
		acquireLock(&b->lock);
		if(b->count < threadCount){
			continueFlag = 0;
		}
		releaseLock(&b->lock);
	}
}

#define KERNEL_STACK_SIZE (8192)
static void wakeupOtherProcessors(LAPIC *lapic, IOAPIC *ioapic, TimerEventList *timer){
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
			sti();
			kernelSleep(timer, 200);
			cli();
		}
	}
}

static void initMultiprocessor(
	int isBSP, InterruptTable *t,
	LAPIC *lapic, IOAPIC *ioapic, TimerEventList *timer){	return;//TODO: stack for AP
	static SpinlockBarrier barrier1, barrier2;
	if(isBSP){
		resetBarrier(&barrier1);
		resetBarrier(&barrier2);
		wakeupOtherProcessors(lapic, ioapic, timer);
	}
	// synchronize
	syncBarrier(&barrier1, getNumberOfLAPIC(ioapic));
	if(isBSP){
		assert(t == global.idt);
		initMultiprocessorPaging(t);
	}
	syncBarrier(&barrier2, getNumberOfLAPIC(ioapic));
}

PIC *castAPIC(APIC *apic){
	return &apic->this;
}

static ProcessorLocal *lapicToProcLocal = NULL;
GetProcessorLocal getProcessorLocal = NULL;

static ProcessorLocal *getProcessorLocalByLAPIC(void){
	assert(getEFlags().bit.interrupt == 0);
	uint32_t id = getMemoryMappedLAPICID();
	return &lapicToProcLocal[id];
}

static ProcessorLocal *getProcessorLocal0(void){
	return &lapicToProcLocal[0];
}

GetProcessorLocal initProcessorLocal(void){
	if(lapicToProcLocal == NULL){
		NEW_ARRAY(lapicToProcLocal, MAX_LAPIC_ID);
		memset(lapicToProcLocal, 0, MAX_LAPIC_ID * sizeof(lapicToProcLocal[0]));
	}
	if(isAPICSupported() == 0){
		getProcessorLocal = getProcessorLocal0;
	}
	else{
		getProcessorLocal = getProcessorLocalByLAPIC;
	}
	return getProcessorLocal;
}

PIC *createPIC(InterruptTable *t){
	if(isAPICSupported() == 0){
		PIC8259 *pic8259 = initPIC8259(t);
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
		apic->ioapic = ioapic;
		printk("number of processors = %d\n", getNumberOfLAPIC(ioapic));
	}
	else{
		apic->ioapic = ioapic;
	}
	return castAPIC(apic);
}

void initLocalTimer(PIC *pic, InterruptTable *t, TimerEventList *timer){
	if(isAPICSupported() == 0){
		setTimer8254Frequency(TIMER_FREQUENCY);
		replaceTimerHandler(timer, pic->irqToVector(pic, TIMER_IRQ));
		pic->setPICMask(pic, TIMER_IRQ, 0);
		return;
	}
	if(isBSP(pic->apic->lapic)){
		testAndResetLAPICTimer(pic->apic->lapic, pic);
		replaceTimerHandler(timer, getTimerVector(pic->apic->lapic));
	}
	else{
		resetLAPICTimer(pic->apic->lapic);
		replaceTimerHandler(timer, getTimerVector(pic->apic->lapic));
	}
	initMultiprocessor(isBSP(pic->apic->lapic), t, pic->apic->lapic, pic->apic->ioapic, timer); // TODO: move elsewhere
}
