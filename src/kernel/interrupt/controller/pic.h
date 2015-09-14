#include<std.h>
#include"interrupt/handler.h"

typedef struct APIC APIC;
typedef struct PIC8259 PIC8259;

typedef struct InterruptController{
	union{
		APIC *apic;
		PIC8259 *pic8259;
	};
	int numberOfProcessors;
	InterruptVector *(*irqToVector)(struct InterruptController *pic, enum IRQ irq);
	void (*setPICMask)(struct InterruptController *pic, enum IRQ irq, int setMask);
	void (*endOfInterrupt)(InterruptParam *p);
	void (*interruptAllOther)(struct InterruptController *pic, InterruptVector *vector);
}PIC;

typedef struct InterruptTable InterruptTable;
typedef struct TimerEventList TimerEventList;
typedef struct ProcessorLocal ProcessorLocal;
// use PIC8259 or APIC
PIC *createPIC(InterruptTable *t);

// see page.c
void initMultiprocessorPaging(InterruptTable *t);

void initLocalTimer(PIC *pic, InterruptTable *t, TimerEventList *timer);
