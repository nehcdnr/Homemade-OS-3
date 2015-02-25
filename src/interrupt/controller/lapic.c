#include"interrupt/handler.h"
#include"pic_private.h"
#include"pic.h"
#include"io/io.h"
#include"interrupt/interrupt.h"
#include"common.h"
#include"memory/memory.h"
#include"assembly/assembly.h"

int isAPICSupported(void){
	if(cpuid_isSupported() == 0){
		printf("processor does not support CPUID");
		return 0;
	}
	if(cpuid_HasOnChipAPIC() == 0){
		printf("processor does not support local APIC");
		return 0;
	}
	return 1;
}

static void lapicTimerHandler(InterruptParam p){
	//printf("interrupt #%d (lapic timer)\n", toChar(p.vector));
	endOfInterrupt(p.vector);
	sti();
}

static void apicErrorHandler(InterruptParam p){
	printf("interrupt #%d (apic error)\n", toChar(p.vector));
	endOfInterrupt(p.vector);
	sti();
}

static void apicEOI(__attribute__((__unused__)) InterruptVector *v){
	MemoryMappedRegister eoi = (MemoryMappedRegister)0xfee000b0; //TODO: parameterize
	*eoi = 0;
}

static volatile unsigned sleepTicks;
static void tempSleepHandler(InterruptParam p){
	sleepTicks++;
	endOfInterrupt(p.vector);
	//sti();
}

#define LAPIC_TIMER_FREQUENCY (TIMER_FREQUENCY)
static void registerLocalInterrupt(const unsigned base, InterruptTable *t, IOAPIC *apic){
	MemoryMappedRegister
	timer_divide = (MemoryMappedRegister)(base + 0x3e0),
	timer_initialCnt = (MemoryMappedRegister)(base + 0x380),
	timer_currentCnt = (MemoryMappedRegister)(base + 0x390),
	// lvt_cmci = base + 0x2f0,
	lvt_timer = (MemoryMappedRegister)(base + 0x320),
	// lvt_thermalMonitor = base + 0x330,
	// lvt_performanceCounter = base + 0x340,
	// lvt_lint0 = base + 0x350,
	// lvt_lint1 = base + 0x360,
	lvt_error = (MemoryMappedRegister)(base + 0x370);
	// eoi = (MemoryMappedRegister)(base + 0x0b0);
	endOfInterrupt = apicEOI;

	//LVT ERROR
	InterruptVector *errorVector = registerInterrupt(t, apicErrorHandler);
	*lvt_error = (((*lvt_error) & (~0x000100ff)) /*| 0x00010000*/ | toChar(errorVector));
	// APIC Timer
	// 1. mask timer interrupt
	// bit 8~0 = interrupt vector
	// bit 16 = 1: masked, 0: not masked
	// bit 19~17 = 00: one-shot, 01: periodic, 10: TSC-deadline
	*lvt_timer |= 0x00010000;
	// 2. timer freq = bus freq / 1
	/* bit 4~0 =
	0000: divide by 2
	0001: 4
	0010: 8
	0011: 16
	1000: 32
	1001: 64
	1010: 128
	1011: 1
	*/
	*timer_divide = ((*timer_divide & (~0x0000000f)) | (0x00000008));
	// 3. test LAPIC timer frequency
	#define FREQ_DIV (10)
	unsigned cnt;
	{
		static_assert(TIMER_FREQUENCY % FREQ_DIV == 0);
		static_assert(LAPIC_TIMER_FREQUENCY % FREQ_DIV == 0);
		*timer_initialCnt = 0xffffffff;
		setTimerFrequency(TIMER_FREQUENCY);
		InterruptHandler oldHandler = setPICHandler(castAPIC(apic), TIMER_IRQ, tempSleepHandler);
		setPICMask(castAPIC(apic), TIMER_IRQ, 0);
		sleepTicks = 0;
		sti();
		while(sleepTicks < 2){
			hlt();
		} // begin of sleeping
		sleepTicks = 0;
		cnt = *timer_currentCnt;
		while(sleepTicks < TIMER_FREQUENCY / FREQ_DIV){
			hlt();
		}
		unsigned cnt2 = *timer_currentCnt;
		cli(); // end of sleeping
		setPICMask(castAPIC(apic), TIMER_IRQ, 1);
		setPICHandler(castAPIC(apic), TIMER_IRQ, oldHandler);
		cnt -= cnt2;
	}
	printf("LAPIC timer frequency = %u kHz\n", (cnt / 1000) * FREQ_DIV);
	*timer_initialCnt = cnt / (LAPIC_TIMER_FREQUENCY / FREQ_DIV);
	#undef FREQ_DIV
	// 4. unmask timer interrupt and set vector
	InterruptVector *timerVector = registerInterrupt(t, lapicTimerHandler);
	*lvt_timer = (((*lvt_timer) & (~0x000700ff)) | 0x00020000 | toChar(timerVector));
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

struct LAPIC{
	unsigned base;
	int isBSP;
};

static void deliverIPI(const unsigned base, int targetLAPICID, enum IPIDeliveryMode mode, short vector){
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
	icr0_32 = (MemoryMappedRegister)(base + 0x300),
	icr32_64 = (MemoryMappedRegister)(base + 0x310);
	*icr32_64 = (((*icr32_64) & (~0xff000000)) | (targetLAPICID << 24));
	*icr0_32 = (((*icr0_32) & (~0x000ccfff)) | 0x00004000 | (mode << 8) | vector);
	while((*icr0_32) & (1<<12));
}

void interprocessorINIT(LAPIC *lapic, int targetLAPICID){
	deliverIPI(lapic->base, targetLAPICID, INIT, 0);
}
void interprocessorSTARTUP(LAPIC *lapic, int targetLAPICID, unsigned int entryAddress){
	assert((entryAddress & (~0x000ff000)) == 0);
	deliverIPI(lapic->base, targetLAPICID, STARTUP, (entryAddress >> 12));
}

static void spuriousInterruptHandler(__attribute__((__unused__)) InterruptParam param){
	sti();
	// not eoi
}

#define KERNEL_STACK_SIZE (16384)
char **initialESP;

static void initOtherProcessors(MemoryManager *m, LAPIC *lapic, IOAPIC *ioapic){
	const int pc = getProcessorCount(ioapic);
	initialESP = allocate(m, sizeof(char*) * pc);
	int i;
	// initialESP[0] = 0x7000; entry.asm
	for(i = 1; i < pc; i++){
		initialESP[i] = allocate(m, KERNEL_STACK_SIZE);
		initialESP[i] += KERNEL_STACK_SIZE;
	}
	for(i = 1; i < pc; i++){
		interprocessorINIT(lapic, i);
	}
	for(i = 0; i < 20000000; i++){
		nop();
	}
	for(i = 1; i < pc; i++){
		interprocessorSTARTUP(lapic, i, 0x7000);
	}
}

LAPIC *initLocalAPIC(MemoryManager *m, InterruptTable *t, IOAPIC *ioapic){
	LAPIC *lapic = allocate(m, sizeof(LAPIC));

	unsigned edx, eax;
	rdmsr(IA32_APIC_BASE, &edx,&eax);
	printf("IA32_APIC_BASE MSR = %x:%x\n", edx, eax);
	if((edx & 0xf) != 0){
		panic("address of APIC base >= 4G");
	}
	lapic->base = (eax & (unsigned int)(~4095));
	lapic->isBSP = ((eax >> 8) & 1);

	printf("APIC Base = %x\n", lapic->base);
	// printf("This is %s Processor\n", (lapic->isBSP? "Bootstrap": "Application"));
	if(((eax >> 11) & 1) == 0){ // is APIC enabled
		printf("enable APIC\n");
		eax |= (1 << 11);
		wrmsr(IA32_APIC_BASE, edx, eax);
	}
	const unsigned base = lapic->base;
	MemoryMappedRegister svr = (MemoryMappedRegister)(base + 0x0f0);
	InterruptVector *vector = registerSpuriousInterrupt(t, spuriousInterruptHandler);
	if(((*svr) & (1 << 8)) == 0){
		printf("enable APIC in SVR\n");
	}
	*svr = (((*svr) & (~0x1ff)) | 0x100 | toChar(vector));
	printf("SVR = %x\n", *svr);

	registerLocalInterrupt(base, t, ioapic);
	initOtherProcessors(m, lapic, ioapic);
	return lapic;
}
