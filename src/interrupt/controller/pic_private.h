#include"interrupt/handler.h"
// APIC
typedef volatile unsigned *const MemoryMappedRegister;

// InterruptTable
InterruptHandler setIRQHandler(InterruptVector *vectorBase, int irq, InterruptHandler handler);

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
int getProcessorCount(IOAPIC *apic);
PIC *castAPIC(IOAPIC *apic);

// local APIC
typedef struct LAPIC LAPIC;
LAPIC *initLocalAPIC(MemoryManager *m, InterruptTable *t, IOAPIC *apic);
void interprocessorINIT(LAPIC *lapic, int targetLAPICID);
void interprocessorSTARTUP(LAPIC *lapic, int targetLAPICID, unsigned int entryAddress);
