#include<std.h>
#include"interrupt/handler.h"

typedef struct APIC APIC;
typedef struct PIC8259 PIC8259;

typedef struct InterruptController{
	union{
		APIC *apic;
		PIC8259 *pic8259;
	};
	InterruptVector *(*irqToVector)(struct InterruptController *pic, enum IRQ irq);
	void (*setPICMask)(struct InterruptController *pic, enum IRQ irq, int setMask);
	void (*endOfInterrupt)(InterruptParam *p);
}PIC;

typedef struct InterruptTable InterruptTable;
typedef struct TimerEventList TimerEventList;
typedef struct ProcessorLocal ProcessorLocal;
// use PIC8259 or APIC
PIC *initPIC(InterruptTable *t);
void initLocalTimer(PIC *pic, TimerEventList *timer);
