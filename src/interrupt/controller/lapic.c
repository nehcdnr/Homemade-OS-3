#include"interrupt/handler.h"
#include"pic_private.h"
#include"pic.h"
#include"io/io.h"
#include"interrupt/interrupt.h"
#include"common.h"
#include"memory/memory.h"
#include"assembly/assembly.h"
enum APICRegisterOffset{
	LVT_TIMER_VECTOR = 0x320,
	TIMER_DIVIDE = 0x3e0,
	TIMER_INITIAL_COUNT = 0x380,
	TIMER_CURRENT_COUNT = 0x390,
	END_OF_INTERRUPT = 0x0b0,
	SPURIOUS_VECTOR = 0x0f0,
	LVT_ERROR_VECTOR = 0x370,
	ICR_LOW = 0x300,
	ICR_HIGH = 0x310,
	LAPIC_ID = 0x020,

	LVT_CMCI = 0x2f0,
	LVT_THERMAL_MONITOR = 0x330,
	LVT_PERFORMANCE_COUNTER = 0x340,
	LVT_LINT0 = 0x350,
	LVT_LINT1 = 0x360
};
static const uintptr_t apicBase = 0xfee00000;

int isAPICSupported(void){
	static char result = 2;
	if(result != 2)
		return result;
	if(cpuid_isSupported() == 0){
		printk("processor does not support CPUID");
		result = 0;
		return 0;
	}
	if(cpuid_hasAPIC() == 0){
		printk("processor does not support local APIC");
		result = 0;
		return 0;
	}
	result = 1;
	return result;
}

static void apicSpuriousHandler(InterruptParam *param){
	printk("spurious interrupt on processor %u\n", param->argument);
	// sti();
	// not eoi
}

static InterruptVector *registerAPICSpurious(const uintptr_t base, const uint32_t lapicID, InterruptTable *t){
	MemoryMappedRegister svr = (MemoryMappedRegister)(base + SPURIOUS_VECTOR);
	printk("SVR = %x\n", *svr);

	InterruptVector *vector = registerInterrupt(t, SPURIOUS_INTERRUPT, apicSpuriousHandler, (uintptr_t)lapicID);
	if(((*svr) & (1 << 8)) == 0){
		printk("enable APIC in SVR\n");
	}
	*svr = (((*svr) & (~0x000001ff)) | 0x00000100 | toChar(vector));
	return vector;
}

static void apicErrorHandler(InterruptParam *p){
	printk("APIC error on processor %u\n", toChar(p->vector), p->argument);
	endOfInterrupt(p);
	panic("APIC error");
	sti();
}

static InterruptVector *registerAPICError(const uintptr_t base, uint32_t lapicID, InterruptTable *t){
	MemoryMappedRegister lvt_error = (MemoryMappedRegister)(base + LVT_ERROR_VECTOR);
	InterruptVector *errorVector = registerGeneralInterrupt(t, apicErrorHandler, (uintptr_t)lapicID);
	*lvt_error = (((*lvt_error) & (~0x000100ff)) /*| 0x00010000*/ | toChar(errorVector));
	return errorVector;
}

static volatile uint32_t sleepTicks;
static void tempSleepHandler(InterruptParam *p){
	sleepTicks++;
	endOfInterrupt(p);
	//sti();
}

static InterruptVector *registerAPICTimer(const uintptr_t base, InterruptTable *t){
	MemoryMappedRegister lvt_timer = (MemoryMappedRegister)(base + LVT_TIMER_VECTOR);
	// mask timer interrupt and set vector
	InterruptVector *timerVector = registerGeneralInterrupt(t, defaultInterruptHandler, 0);
	*lvt_timer = (((*lvt_timer) & (~0x000700ff)) | 0x00020000 | toChar(timerVector));
	return timerVector;
}

enum IPIDeliveryMode{
	FIXED = 0,
	/*
	LOWEST_PRIORITY = 1,
	SMI = 2,
	NMI = 4,
	*/
	INIT = 5,
	STARTUP = 6
};
enum IPIShortHand{
	NONE = 0,
	SELF = 1,
	ALL_INCLUDING_SELF = 2,
	ALL_EXCLUDING_SELF = 3
};

static void deliverIPI(const uintptr_t base, uint32_t targetLAPICID, enum IPIDeliveryMode mode, uint16_t vector){
	/*
	bit 8~0: vector number
	11~8: delivery mode
	12~11: physical destination = 0, logical destination = 1
	13~12: idle = 0, pending = 1
	15~14: deassert = 0, assert = 1
	16~15: edge trigger = 0, level trigger = 1
	18~20: shorthand
	64~56: destination
	*/
	MemoryMappedRegister
	icr0_32 = (MemoryMappedRegister)(base + ICR_LOW),
	icr32_64 = (MemoryMappedRegister)(base + ICR_HIGH);
	*icr32_64 = (((*icr32_64) & (~0xff000000)) | (targetLAPICID << 24));
	*icr0_32 = (((*icr0_32) & (~0x000ccfff)) | 0x00004000 | (mode << 8) | vector);
	while((*icr0_32) & (1<<12));
}

struct LAPIC{
	uintptr_t base;
	int isBSP;
	uint32_t lapicID;
	InterruptVector *spuriousVector;
	InterruptVector *timerVector;
	InterruptVector *errorVector;
};

int isBSP(LAPIC *lapic){
	return lapic->isBSP;
}

InterruptVector *getTimerVector(LAPIC *lapic){
	return lapic->timerVector;
}

uint32_t getLAPICID(LAPIC *lapic){
	return lapic->lapicID;
}

static uint32_t testLAPICTimerFrequency(const uintptr_t base, const uint32_t ticks, IOAPIC *apic){
	MemoryMappedRegister
	timer_initialCnt = (MemoryMappedRegister)(base + TIMER_INITIAL_COUNT),
	timer_currentCnt = (MemoryMappedRegister)(base + TIMER_CURRENT_COUNT);
	uint32_t cnt1, cnt2;
	setTimer8254Frequency(TIMER_FREQUENCY);
	InterruptVector *timerVector = irqToVector(castAPIC(apic), TIMER_IRQ);
	InterruptHandler h = tempSleepHandler;
	uintptr_t a = 0;
	*timer_initialCnt = 0xffffffff;
	replaceHandler(timerVector, &h, &a);
	setPICMask(castAPIC(apic), TIMER_IRQ, 0);
	sleepTicks = 0;
	sti();
	while(sleepTicks < 2){
		hlt();
	} // begin sleeping
	sleepTicks = 0;
	cnt1 = *timer_currentCnt;
	while(sleepTicks < ticks){
		hlt();
	}
	cnt2 = *timer_currentCnt;
	cli(); // end sleeping
	setPICMask(castAPIC(apic), TIMER_IRQ, 1);
	replaceHandler(timerVector, &h, &a);
	return cnt1 - cnt2;
}

void testAndResetLAPICTimer(LAPIC *lapic, IOAPIC *ioapic){
	MemoryMappedRegister
	lvt_timer = (MemoryMappedRegister)(lapic->base + LVT_TIMER_VECTOR),
	timer_divide = (MemoryMappedRegister)(lapic->base + TIMER_DIVIDE),
	timer_initialCnt = (MemoryMappedRegister)(lapic->base + TIMER_INITIAL_COUNT);
	// 1. mask timer interrupt
	// bit 8~0 = interrupt vector
	// bit 16 = 1: masked, 0: not masked
	// bit 19~17 = 00: one-shot, 01: periodic, 10: TSC-deadline
	uint32_t oldLVT_TIMER = *lvt_timer;
	*lvt_timer |= 0x00010000;
	// 2. timer freq = bus freq / divisor
	/* bit 4~0 = divisor
	0000: 2; 0001: 4; 0010: 8; 0011: 16;
	1000: 32; 1001: 64 1010: 128; 1011: 1
	*/
	*timer_divide = ((*timer_divide & (~0x0000000f)) | (0x00000008));
	// 3. test LAPIC timer frequency
	#define FREQ_DIV (10)
	static uint32_t lastResult = 1000000000;
	if(ioapic != NULL){
		static_assert(TIMER_FREQUENCY % FREQ_DIV == 0);
		lastResult = testLAPICTimerFrequency(lapic->base, TIMER_FREQUENCY / FREQ_DIV, ioapic);
	}
	// kprintf("LAPIC timer frequency = %u kHz\n", (cnt / 1000) * FREQ_DIV);
	*timer_initialCnt = lastResult / (TIMER_FREQUENCY / FREQ_DIV);
	#undef FREQ_DIV
	*lvt_timer = oldLVT_TIMER;
}

void resetLAPICTimer(LAPIC *lapic){
	testAndResetLAPICTimer(lapic, NULL);
}

void interprocessorINIT(LAPIC *lapic, uint32_t targetLAPICID){
	deliverIPI(lapic->base, targetLAPICID, INIT, 0);
}
void interprocessorSTARTUP(LAPIC *lapic, uint32_t targetLAPICID, uintptr_t entryAddress){
	assert((entryAddress & (~0x000ff000)) == 0);
	deliverIPI(lapic->base, targetLAPICID, STARTUP, (entryAddress >> 12));
}

static void apicEOI(InterruptParam *p){
	MemoryMappedRegister eoi =
	(MemoryMappedRegister)(p->processorLocal->lapic->base + END_OF_INTERRUPT);
	*eoi = 0;
}

LAPIC *initLocalAPIC(MemoryManager *m, InterruptTable *t, ProcessorLocal *pl){
	LAPIC *NEW(lapic, m);

	uint32_t edx, eax;
	rdmsr(IA32_APIC_BASE, &edx, &eax);
	printk("IA32_APIC_BASE MSR = %x:%x\n", edx, eax);
	if((edx & 0xf) != 0 || (eax & 0xfffff000) != apicBase){
		printk("relocate apic base address to %x", apicBase);
		edx = (edx & ~0xf);
		eax = ((eax & ~0xfffff000) | apicBase);
		wrmsr(IA32_APIC_BASE, edx, eax);
	}
	if(((eax >> 11) & 1) == 0){ // is APIC enabled
		printk("enable APIC\n");
		eax |= (1 << 11);
		wrmsr(IA32_APIC_BASE, edx, eax);
	}
	endOfInterrupt = apicEOI;

	lapic->base = (apicBase & 0xfffff000);
	lapic->isBSP = ((eax >> 8) & 1);
	MemoryMappedRegister lapicIDAddress = (MemoryMappedRegister)(lapic->base + LAPIC_ID);
	lapic->lapicID = (((*lapicIDAddress) >> 24) & 0xff);
	lapic->spuriousVector = registerAPICSpurious(lapic->base, lapic->lapicID, t);
	lapic->errorVector = registerAPICError(lapic->base, lapic->lapicID, t);
	lapic->timerVector = registerAPICTimer(lapic->base, t);

	pl->lapic = lapic;
	return lapic;
}
