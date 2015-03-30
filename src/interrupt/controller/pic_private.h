#include"pic.h"
#include"multiprocessor/processorlocal.h"

// interruptdescriptor.c
InterruptVector *getVector(InterruptVector *base, int irq);

// 8259 PIC
typedef struct PIC8259 PIC8259;
void disablePIC8259(void);
PIC8259 *initPIC8259(InterruptTable *t);
PIC *castPIC8259(PIC8259 *pic8259);

// I/O APIC
typedef struct IOAPIC IOAPIC;

int isAPICSupported(void);
IOAPIC *initAPIC(InterruptTable *t);
int getNumberOfLAPIC(IOAPIC *apic);
uint32_t getLAPICIDByIndex(IOAPIC *apic, int index);


void apic_setPICMask(PIC *pic, enum IRQ irq, int setMask);
InterruptVector *apic_irqToVector(PIC *pic, enum IRQ irq);

// local APIC
typedef struct LAPIC LAPIC;
LAPIC *initLocalAPIC(InterruptTable *t);
InterruptVector *getTimerVector(LAPIC *lapic);
int isBSP(LAPIC *lapic);
uint32_t getLAPICID(LAPIC *lapic);
void testAndResetLAPICTimer(LAPIC *lapic, PIC* pic);
void resetLAPICTimer(LAPIC *lapic);
void interprocessorINIT(LAPIC *lapic, uint32_t targetLAPICID);
void interprocessorSTARTUP(LAPIC *lapic, uint32_t targetLAPICID, uintptr_t entryAddress);

void apic_endOfInterrupt(InterruptParam *p);

// APIC
typedef struct APIC{
	PIC this;
	LAPIC *lapic;
	IOAPIC *ioapic;
}APIC;
typedef volatile uint32_t *const MemoryMappedRegister;

PIC *castAPIC(APIC *apic);
