#include"interrupt/handler.h"
#include"interrupt.h"
#include"controller/pic_private.h"
#include"common.h"
#include"assembly/assembly.h"
#include"memory/memory.h"

// miss a timer interrupt if 8259 irq 0 is not mapped to vector 32
#define SPURIOUS_VECTOR (127)
#define BEGIN_GENERAL_VECTOR (32)
#define END_GENERAL_VECTOR (127)

typedef struct{
	unsigned short handler_0_16;
	unsigned short segmentSelector;
	unsigned char reserved;
	unsigned char gateType: 3;
	unsigned char bit32: 1;
	unsigned char reserved2: 1;
	unsigned char privilege: 2;
	unsigned char present: 1;
	unsigned short handler_16_32;
}InterruptDescriptor;
static_assert(sizeof(InterruptDescriptor) == 8);

#define INVALID_IRQ (-1)
struct InterruptVector{
	unsigned char charValue;
	int irq;
};

struct InterruptTable{
	int length;
	int usedCount;
	InterruptVector *vector;
	InterruptDescriptor *descriptor;
};

// handler.asm
// delivery: pic -> idt -> entry -> handler
extern struct{
	volatile InterruptVector *vectorAddress;
	volatile InterruptHandler handler;
	void (*const entry)(void);
}asmIntEntry[];
extern const int asmIntEntryCount;

static_assert(sizeof(asmIntEntry[0]) == 12);

void defaultInterruptHandler(InterruptParam param){
	printf("unhandled interrupt %d\n", toChar(param.vector));
	printf("cs=%u , ds=%u , error=%u, eip=%u, eflags=%x eax=%x\n",
	param.cs, param.ds, param.errorCode, param.eip, param.eflags,param.eax);
	panic("unhandled interrupt");
	sti();
}

InterruptHandler setInterruptHandler(InterruptVector *v, InterruptHandler handler){
	InterruptHandler oldHandler = asmIntEntry[toChar(v)].handler;
	asmIntEntry[toChar(v)].handler = handler;
	return oldHandler;
}

InterruptVector *registerInterrupt(InterruptTable *t, InterruptHandler handler){
	assert(t->usedCount < t->length && t->usedCount < END_GENERAL_VECTOR);
	asmIntEntry[t->usedCount].handler = handler;
	t->vector[t->usedCount].irq = INVALID_IRQ;
	t->usedCount++;
	return t->vector + t->usedCount - 1;
}

InterruptHandler setIRQHandler(InterruptVector *vectorBase, int irq, InterruptHandler handler){
	assert(vectorBase[irq].irq == irq);
	// printf("map IRQ %d to vector %d\n", irq, toChar(vectorBase) + irq);
	InterruptHandler oldHandler = asmIntEntry[toChar(vectorBase + irq)].handler;
	asmIntEntry[toChar(vectorBase + irq)].handler = handler;
	return oldHandler;
}

InterruptVector *registerIRQs(InterruptTable *t, int irqBegin, int irqCount){
	assert(t->usedCount + irqCount  <= t->length && t->usedCount + irqCount < END_GENERAL_VECTOR);
	int i;
	for(i = 0; i < irqCount; i++){
		t->vector[t->usedCount + i].irq = irqBegin + i;
	}
	t->usedCount += irqCount;
	return t->vector + t->usedCount - irqCount;
}

static_assert((SPURIOUS_VECTOR & 0xf) == 0xf);
InterruptVector *registerSpuriousInterrupt(InterruptTable *t, InterruptHandler handler){
	assert(SPURIOUS_VECTOR < t->length);
	t->vector[SPURIOUS_VECTOR].irq = INVALID_IRQ;
	asmIntEntry[SPURIOUS_VECTOR].handler = handler;
	return t->vector + SPURIOUS_VECTOR;
}

unsigned char toChar(InterruptVector *v){
	return v->charValue;
}

int getIRQ(InterruptVector *v){
	assert(v->irq != INVALID_IRQ);
	return v->irq;
}

// end of interrupt
static void noEOI(InterruptVector *v){
	printf("noEOI(vector = %d)\n", toChar(v));
	panic("EOI not registered");
}

void (*volatile endOfInterrupt)(InterruptVector*) = noEOI;

// segmentdescriptor.h
unsigned short toShort(SegmentSelector* s);

InterruptTable *initInterruptTable(MemoryManager *m, SegmentSelector *kernelCodeSelector){
	struct InterruptTable *t = allocate(m, sizeof(InterruptTable));
	t->descriptor = allocateAligned(m, asmIntEntryCount * sizeof(InterruptDescriptor), sizeof(InterruptDescriptor));
	t->vector = allocate(m, asmIntEntryCount * sizeof(struct InterruptVector));
	t->length = asmIntEntryCount;
	t->usedCount = BEGIN_GENERAL_VECTOR;
	printf("number of interrupt handlers = %d\n", t->length);

	int i;
	for(i = 0; i < t->length; i++){
		t->vector[i].charValue = i;
		t->vector[i].irq = INVALID_IRQ;
		asmIntEntry[i].vectorAddress = t->vector + i;
		asmIntEntry[i].handler = defaultInterruptHandler;
		InterruptDescriptor *d = t->descriptor + i;
		d->handler_0_16 =  ((unsigned)(asmIntEntry[i].entry)) % (1 << 16);
		d->handler_16_32 = ((unsigned)(asmIntEntry[i].entry)) / (1 << 16);
		d->segmentSelector = toShort(kernelCodeSelector);
		d->reserved = 0;
		d->gateType = 6;
		d->bit32 = 1;
		d->reserved2 = 0;
		d->privilege = 0;
		d->present = 1;
	}

	return t;
}

void lidt(InterruptTable *idt){
	unsigned short lidt[3] = {
		idt->length * sizeof(InterruptDescriptor) - 1,
		((unsigned)(idt->descriptor)) % 65536,
		((unsigned)(idt->descriptor)) / 65536
	};
	__asm__(
	"lidtl %0\n"
	:
	:"m"(lidt)
	);
}
