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
	uint16_t handler_0_16;
	uint16_t segmentSelector;
	uint8_t reserved;
	uint8_t gateType: 3;
	uint8_t bit32: 1;
	uint8_t reserved2: 1;
	uint8_t privilege: 2;
	uint8_t present: 1;
	uint16_t handler_16_32;
}InterruptDescriptor;
static_assert(sizeof(InterruptDescriptor) == 8);

#define INVALID_IRQ (-1)
struct InterruptVector{
	uint8_t charValue;
	int irq;
	InterruptTable *table;
};

// handler.asm
// delivery: pic -> idt -> entry -> handler
typedef struct{
	const uint32_t entryOffset;
	volatile InterruptHandler handler;
	volatile InterruptVector *vector;
	volatile uintptr_t arg;
}AsmIntEntry;
static_assert(sizeof(AsmIntEntry) == 16);

extern const AsmIntEntry intEntriesTemplate[];
extern const size_t sizeOfIntEntries;
extern const int numberOfIntEntries;
extern const size_t intEntryBaseOffset;

struct InterruptTable{
	int length;
	int usedCount;
	AsmIntEntry *asmIntEntry;
	InterruptVector *vector;
	InterruptDescriptor *descriptor;
};

static AsmIntEntry *createAsmIntEntries(MemoryManager *m){
	AsmIntEntry *e = allocate(m, sizeOfIntEntries);
	memcpy(e, intEntriesTemplate, sizeOfIntEntries);

	uintptr_t *inst = (uintptr_t*)(intEntryBaseOffset + (uintptr_t)e);
	*inst = (uintptr_t)e;
	return e;
}

void defaultInterruptHandler(volatile InterruptParam param){
	kprintf("unhandled interrupt %d parameter = %x\n", toChar(param.vector), param.argument);
	kprintf("cs=%u , ds=%u , error=%u, eip=%u, eflags=%x eax=%x\n",
	param.cs, param.ds, param.errorCode, param.eip, param.eflags, param.eax);
	panic("unhandled interrupt");
	sti();
}

InterruptVector *registerInterrupt(InterruptTable *t, InterruptHandler handler, uintptr_t arg){
	assert(t->usedCount < t->length && t->usedCount < END_GENERAL_VECTOR);
	t->asmIntEntry[t->usedCount].handler = handler;
	t->asmIntEntry[SPURIOUS_VECTOR].arg = arg;
	t->vector[t->usedCount].irq = INVALID_IRQ;
	t->usedCount++;
	return t->vector + t->usedCount - 1;
}

void replaceHandler(InterruptVector *vector, InterruptHandler *handler, uintptr_t *arg){
	InterruptTable *t = vector->table;
	const int i = toChar(vector);
	// printf("map IRQ %d to vector %d\n", irq, toChar(vectorBase) + irq);
	InterruptHandler oldHandler = t->asmIntEntry[i].handler;
	uintptr_t oldArg = t->asmIntEntry[i].arg;
	t->asmIntEntry[i].handler = *handler;
	t->asmIntEntry[i].arg = *arg;
	*handler = oldHandler;
	*arg = oldArg;
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

InterruptVector *getVector(InterruptVector *base, int irq){
	return base + irq;
}

static_assert((SPURIOUS_VECTOR & 0xf) == 0xf);
InterruptVector *registerSpuriousInterrupt(InterruptTable *t, InterruptHandler handler, uintptr_t arg){
	assert(SPURIOUS_VECTOR < t->length);
	t->vector[SPURIOUS_VECTOR].irq = INVALID_IRQ;
	t->asmIntEntry[SPURIOUS_VECTOR].handler = handler;
	t->asmIntEntry[SPURIOUS_VECTOR].arg = arg;
	return t->vector + SPURIOUS_VECTOR;
}

uint8_t toChar(InterruptVector *v){
	return v->charValue;
}

int getIRQ(InterruptVector *v){
	assert(v->irq != INVALID_IRQ);
	return v->irq;
}

// end of interrupt
static void noEOI(InterruptVector *v){
	kprintf("noEOI(vector = %d)\n", toChar(v));
	panic("EOI not registered");
}

void (*volatile endOfInterrupt)(InterruptVector*) = noEOI;

// segmentdescriptor.h
uint16_t toShort(SegmentSelector* s);

InterruptTable *initInterruptTable(MemoryManager *m, SegmentSelector *kernelCodeSelector){
	struct InterruptTable *t = allocate(m, sizeof(InterruptTable));
	t->descriptor = allocateAligned(m, numberOfIntEntries * sizeof(InterruptDescriptor), sizeof(InterruptDescriptor));
	t->vector = allocate(m, numberOfIntEntries * sizeof(struct InterruptVector));
	t->asmIntEntry = createAsmIntEntries(m);
	t->length = numberOfIntEntries;
	t->usedCount = BEGIN_GENERAL_VECTOR;
	kprintf("number of interrupt handlers = %d\n", t->length);

	int i;
	for(i = 0; i < t->length; i++){
		t->vector[i].charValue = i;
		t->vector[i].irq = INVALID_IRQ;
		t->vector[i].table = t;
		t->asmIntEntry[i].vector = t->vector + i;
		t->asmIntEntry[i].handler = defaultInterruptHandler;
		t->asmIntEntry[i].arg = 0;
		InterruptDescriptor *d = t->descriptor + i;
		uint32_t entryAddress = (uint32_t)(t->asmIntEntry) + t->asmIntEntry[i].entryOffset;

		d->handler_0_16 =  ((entryAddress >> 0) & 0xffff);
		d->handler_16_32 = ((entryAddress >> 16) & 0xffff);
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
	uint16_t lidt[3] = {
		idt->length * sizeof(InterruptDescriptor) - 1,
		((uintptr_t)(idt->descriptor)) % 65536,
		((uintptr_t)(idt->descriptor)) / 65536
	};
	__asm__(
	"lidtl %0\n"
	:
	:"m"(lidt)
	);
}
