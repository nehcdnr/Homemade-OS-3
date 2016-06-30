#include"interrupt/handler.h"
#include"interrupt/systemcalltable.h"
#include"interrupt.h"
#include"internalinterrupt.h"
#include"memory/segment.h"
#include"controller/pic_private.h"
#include"common.h"
#include"kernel.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"task/task.h"
#include"multiprocessor/spinlock.h"

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

typedef struct InterruptHandlerChain{
	ChainedInterruptHandler handler;
	uintptr_t arg;
	struct InterruptHandlerChain **prev, *next;
}InterruptHandlerChain;

#define INVALID_IRQ (-1)
struct InterruptVector{
	uint8_t charValue;
	int irq;
	InterruptTable *table;
	// if irq != INVALID_IRQ
	InterruptHandlerChain *handlerChain;
	Spinlock lock;
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

// see interruptentry.asm
static void chainedInterruptHandler(InterruptParam *p){
	assert(p->argument = 0xffffffff);
	InterruptVector *v = p->vector;
	struct InterruptHandlerChain *c;
	acquireLock(&v->lock);
	int noHandler = (v->handlerChain == NULL);
	int handledCount = 0;
	for(c = v->handlerChain; c != NULL; c=c->next){
		p->argument = c->arg;
		if(c->handler(p)){
			handledCount++;
		}
		assert(getEFlags().bit.interrupt == 0);
	}
	releaseLock(&v->lock);
	if(noHandler){
		defaultInterruptHandler(p);
	}
	if(handledCount == 0){
		printk("unhandled interrupt: %d (irq %d)\n", toChar(v), getIRQ(v));
	}
	processorLocalPIC()->endOfInterrupt(p);
	// not call sti() to avoid stack underflow
}

static InterruptHandlerChain *createIntHandlerChain(ChainedInterruptHandler handler, uintptr_t arg){
	InterruptHandlerChain *NEW(c);
	if(c == NULL){
		return NULL;
	}
	c->handler = handler;
	c->arg = arg;
	c->prev = NULL;
	c->next = NULL;
	return c;
}

// miss a timer interrupt if 8259 irq 0 is not mapped to vector 32
InterruptVector *registerGeneralInterrupt(InterruptTable *t, InterruptHandler handler, uintptr_t arg){
	assert(t->usedCount < t->length && t->usedCount < END_GENERAL_VECTOR);
	t->asmIntEntry[t->usedCount].handler = handler;
	t->asmIntEntry[t->usedCount].arg = arg;
	t->vector[t->usedCount].irq = INVALID_IRQ;
	t->usedCount++;
	return t->vector + t->usedCount - 1;
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

InterruptVector *registerIRQs(InterruptTable *t, int irqBegin, int irqCount){
	assert(t->usedCount + irqCount  <= t->length && t->usedCount + irqCount <= END_GENERAL_VECTOR);
	int i;
	for(i = 0; i < irqCount; i++){
		t->asmIntEntry[t->usedCount + i].handler = chainedInterruptHandler;
		t->asmIntEntry[t->usedCount + i].arg = 0xffffffff;
		t->vector[t->usedCount + i].irq = irqBegin + i;
		t->vector[t->usedCount + i].handlerChain = NULL;
		t->vector[t->usedCount + i].lock = initialSpinlock;
	}
	t->usedCount += irqCount;
	return t->vector + t->usedCount - irqCount;
}

int addHandler(InterruptVector *vector, ChainedInterruptHandler handler, uintptr_t arg){
	assert(vector->irq != INVALID_IRQ);
	InterruptHandlerChain *c = createIntHandlerChain(handler, arg);
	if(c == NULL){
		return 0;
	}
	acquireLock(&vector->lock);
	ADD_TO_DQUEUE(c, &vector->handlerChain);
	releaseLock(&vector->lock);
	return 1;
}

int removeHandler(InterruptVector *vector, ChainedInterruptHandler handler, uintptr_t arg){
	assert(vector->irq != INVALID_IRQ);
	InterruptHandlerChain *c;
	acquireLock(&vector->lock);
	// assume handler and arg are legal
	for(c = vector->handlerChain; c != NULL; c = c->next){
		if(c->handler == handler || c->arg == arg){
			REMOVE_FROM_DQUEUE(c);
			break;
		}
	}
	releaseLock(&vector->lock);
	if(c == NULL){
		return 0;
	}
	DELETE(c);
	return 1;
}

void replaceHandler(InterruptVector *vector, InterruptHandler *handler, uintptr_t *arg){
	InterruptTable *t = vector->table;
	assert(vector->irq == INVALID_IRQ);
	const int i = toChar(vector);
	//printk("map IRQ %d to vector %d\n", vector->irq, toChar(vector));
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
	if(t->vector == NULL){
		panic("cannot allocate vector array\n");
	}
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
	// allow user program to system call
	t->descriptor[SYSTEM_CALL_VECTOR].privilege = 3;

	initInternalInterrupt(t);
	return t;
}

void callHandler(InterruptTable *t, uint8_t intNumber, InterruptParam *p){
	assert(intNumber < t->length);
	InterruptVector *originalVector = p->vector;
	uintptr_t originalArgument = p->argument;
	assert(intNumber != SYSTEM_CALL); // system calls are recognized by eax
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
