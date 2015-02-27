#include"interrupt/handler.h"

// interruptdescriptor.c
InterruptVector *getVector(InterruptVector *base, int irq);

// APIC
typedef volatile uint32_t *const MemoryMappedRegister;

// generic PIC
typedef struct IOAPIC IOAPIC;
typedef struct PIC8259 PIC8259;
typedef union PIC{
	PIC8259 *pic8259;
	IOAPIC *apic;
}PIC;

typedef struct MemoryManager MemoryManager;
typedef struct InterruptTable InterruptTable;
// 8259 PIC
void disablePIC8259(void);
PIC8259 *initPIC8259(MemoryManager *m, InterruptTable *t);
PIC *castPIC8259(PIC8259 *pic8259);

// I/O APIC
int isAPICSupported(void);
IOAPIC *initAPIC(MemoryManager* m, InterruptTable *t);
int getNumberOfLAPIC(IOAPIC *apic);
uint32_t getLAPICIDByIndex(IOAPIC *apic, int index);
PIC *castAPIC(IOAPIC *apic);

// local APIC
typedef struct LAPIC LAPIC;
LAPIC *initLocalAPIC(MemoryManager *m, InterruptTable *t);
InterruptVector *getTimerVector(LAPIC *lapic);
int isBSP(LAPIC *lapic);
uint32_t getLAPICID(LAPIC *lapic);
void testAndResetLAPICTimer(LAPIC *lapic, IOAPIC* ioapic);
void resetLAPICTimer(LAPIC *lapic);
void interprocessorINIT(LAPIC *lapic, uint32_t targetLAPICID);
void interprocessorSTARTUP(LAPIC *lapic, uint32_t targetLAPICID, uintptr_t entryAddress);
