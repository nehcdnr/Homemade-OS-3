#ifndef HANDLER_H_INCLUDED
#define HANDLER_H_INCLUDED

typedef struct InterruptVector InterruptVector;

// handler parameter, see interruptentry.asm
typedef struct InterruptParam{
	InterruptVector *vector;
	unsigned short
	gs, unused0,
	fs, unused1,
	es, unused2,
	ds, unused3;// pushed by os
	unsigned int
	edi, esi, ebp,
	ebx, edx, ecx, eax;
	int errorCode;
	unsigned int
	eip, cs, eflags, // pushed by cpu
	esp, ss; // pushed if privilege changed
}InterruptParam;

// handler
void defaultInterruptHandler(InterruptParam param);

typedef void (*InterruptHandler)(InterruptParam param);
typedef struct InterruptTable InterruptTable;
// vector
InterruptVector *registerInterrupt(InterruptTable *t, InterruptHandler handler);
InterruptHandler setInterruptHandler(InterruptVector *v, InterruptHandler handler);
InterruptVector *registerIRQs(InterruptTable *t, int irqBegin, int irqCount);
InterruptVector *registerSpuriousInterrupt(InterruptTable *t, InterruptHandler handler);

extern void (*volatile endOfInterrupt)(InterruptVector *);

unsigned char toChar(InterruptVector *v);
int getIRQ(InterruptVector *v);


enum IRQ{
	TIMER_IRQ = 0,
	KEYBOARD_IRQ = 1,
	SLAVE_IRQ = 2,
	FLOPPY_IRQ = 6,
	MOUSE_IRQ = 12
};
typedef union PIC PIC;
extern InterruptHandler (*setPICHandler)(PIC *pic, enum IRQ irq, InterruptHandler handler);
extern void (*setPICMask)(PIC *pic, enum IRQ irq, int setMask);

#endif
