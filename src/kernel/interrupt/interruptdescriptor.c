#include"interrupt/handler.h"
#include"interrupt.h"
#include"internalinterrupt.h"
#include"memory/segment.h"
#include"controller/pic_private.h"
#include"common.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"task/task.h"
#include"systemcall.h"

static_assert((SPURIOUS_INTERRUPT & 0xf) == 0xf);

typedef struct{
	uint16_t handler_0_16;
	uint16_t segmentSelector;
	uint8_t reserved;
	uint8_t gateType: 3;
	uint8_t bit32: 1;
	uint8_t reserved2: 1;
	uint8_t privilege: 2; // maximum ring number of caller
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

extern AsmIntEntry intEntries[];
extern const int numberOfIntEntries;
extern const size_t sizeOfIntEntries;
extern AsmIntEntry *intEntriesAddress;

static_assert(sizeof(intEntriesAddress) == 4);
static_assert(sizeof(numberOfIntEntries) <= 4);
static_assert(sizeof(sizeOfIntEntries) <= 4);

struct InterruptTable{
	int length;
	int usedCount;
	AsmIntEntry *asmIntEntry;
	InterruptVector *vector;
	InterruptDescriptor *descriptor;
};

static AsmIntEntry *createAsmIntEntries(void){
	AsmIntEntry *e = intEntries;
	intEntriesAddress = intEntries;
	return e;
}

void defaultInterruptHandler(InterruptParam *p){
	sti();
	printk("unhandled interrupt %d; error = %d eip = %x\n", toChar(p->vector), p->errorCode, p->eip);
	//printk("cs = %x, ss = %x esp = %x eflags = %x\n",p->cs, p->ss, p->esp, p->eflags);
	printk("terminate task %x\n", processorLocalTask());
	terminateCurrentTask();
}

// miss a timer interrupt if 8259 irq 0 is not mapped to vector 32
InterruptVector *registerGeneralInterrupt(InterruptTable *t, InterruptHandler handler, uintptr_t arg){
	assert(t->usedCount < t->length && t->usedCount < END_GENERAL_VECTOR);
	t->asmIntEntry[t->usedCount].handler = handler;
	t->asmIntEntry[SPURIOUS_INTERRUPT].arg = arg;
	t->vector[t->usedCount].irq = INVALID_IRQ;
	t->usedCount++;
	return t->vector + t->usedCount - 1;
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

InterruptVector *registerInterrupt(
	InterruptTable *t,
	enum ReservedInterruptVector i,
	InterruptHandler handler,
	uintptr_t arg
){
	assert(((int)i) < t->length);
	t->vector[i].irq = INVALID_IRQ;
	t->asmIntEntry[i].handler = handler;
	t->asmIntEntry[i].arg = arg;
	return t->vector + i;
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

void setHandler(InterruptVector *vector, InterruptHandler handler, uintptr_t arg){
	replaceHandler(vector, &handler, &arg);
}

InterruptVector *getVector(InterruptVector *base, int irq){
	return base + irq;
}

uint8_t toChar(InterruptVector *v){
	return v->charValue;
}

int getIRQ(InterruptVector *v){
	assert(v->irq != INVALID_IRQ);
	return v->irq;
}

// end of interrupt
static void noEOI(InterruptParam *p){
	printk("noEOI(vector = %d)\n", toChar(p->vector));
	panic("EOI not registered");
}

void (*endOfInterrupt)(InterruptParam *p) = noEOI;

InterruptTable *initInterruptTable(SegmentTable *gdt){
	struct InterruptTable *NEW(t);
	{
		uintptr_t desc = (uintptr_t)allocateKernelMemory((numberOfIntEntries + 1) * sizeof(InterruptDescriptor));
		while(desc % sizeof(InterruptDescriptor) != 0){
			desc++;
		}
		t->descriptor = (InterruptDescriptor*)desc;
	}
	NEW_ARRAY(t->vector, numberOfIntEntries);
	t->asmIntEntry = createAsmIntEntries();
	t->length = numberOfIntEntries;
	t->usedCount = BEGIN_GENERAL_VECTOR;
	printk("number of interrupt handlers = %d\n", t->length);

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
		d->segmentSelector = getSegmentSelector(gdt, GDT_KERNEL_CODE_INDEX).value;
		d->reserved = 0;
		d->gateType = 6;
		d->bit32 = 1;
		d->reserved2 = 0;
		d->privilege = 0;
		d->present = 1;
	}
	initInternalInterrupt(t);
	return t;
}

void callHandler(InterruptTable *t, uint8_t intNumber, InterruptParam *p){
	assert(intNumber < t->length);
	InterruptVector *originalVector = p->vector;
	uintptr_t originalArgument = p->argument;
	p->vector = t->vector + intNumber;
	p->argument = t->asmIntEntry[intNumber].arg;
	t->asmIntEntry[intNumber].handler(p);
	p->argument = originalArgument;
	p->vector = originalVector;
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