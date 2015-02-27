#ifndef HANDLER_H_INCLUDED
#define HANDLER_H_INCLUDED
#include<std.h>

typedef struct InterruptVector InterruptVector;

// handler parameter, see interruptentry.asm
typedef struct InterruptParam{
	uintptr_t argument; // pushed by os
	InterruptVector *vector;
	uint32_t
	gs, fs, es, ds,
	edi, esi, ebp,
	ebx, edx, ecx, eax,
	errorCode, // pushed by cpu or os
	eip, cs, eflags, // pushed by cpu
	esp, ss; // pushed if privilege changed
}InterruptParam;

// handler
void defaultInterruptHandler(volatile InterruptParam param);

typedef void (*InterruptHandler)(volatile InterruptParam param);
typedef struct InterruptTable InterruptTable;
// vector
InterruptVector *registerInterrupt(InterruptTable *t, InterruptHandler handler, uintptr_t arg);
InterruptVector *registerIRQs(InterruptTable *t, int irqBegin, int irqCount);
InterruptVector *registerSpuriousInterrupt(InterruptTable *t, InterruptHandler handler, uintptr_t arg);
void replaceHandler(InterruptVector *v, InterruptHandler *handler, uintptr_t *arg);

extern void (*volatile endOfInterrupt)(InterruptVector *);

uint8_t toChar(InterruptVector *v);
int getIRQ(InterruptVector *v);


enum IRQ{
	TIMER_IRQ = 0,
	KEYBOARD_IRQ = 1,
	SLAVE_IRQ = 2,
	FLOPPY_IRQ = 6,
	MOUSE_IRQ = 12
};
typedef union PIC PIC;
extern InterruptVector *(*irqToVector)(PIC *pic, enum IRQ irq);
extern void (*setPICMask)(PIC *pic, enum IRQ irq, int setMask);

#endif
