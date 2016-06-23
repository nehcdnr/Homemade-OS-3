#include"pic_private.h"
#include"interrupt/interrupt.h"
#include"memory/memory.h"
#include"common.h"
#include"kernel.h"
#include"assembly/assembly.h"

// refer to chipset datasheet
enum{
	M_ICW1 = 0x20,
	M_ICW2 = 0x21,
	M_ICW3 = 0x21,
	M_ICW4 = 0x21,
	S_ICW1 = 0xa0,
	S_ICW2 = 0xa1,
	S_ICW3 = 0xa1,
	S_ICW4 = 0xa1,

	M_OCW1 = 0x21, // interrupt mask
	M_OCW2 = 0x20, // end of interrupt
	S_OCW1 = 0xa1,
	S_OCW2 = 0xa0,
};

static void pic8259_endOfInterrupt(InterruptParam *p){
	int irq = getIRQ(p->vector);
	assert(irq >= 0 && irq < 16);
	if(irq >= 8){
		out8(S_OCW2, 0x20);
	}
	out8(M_OCW2, 0x20);
}

typedef struct PIC8259{
	PIC this;
	InterruptTable *interruptTable;
	InterruptVector *vectorBase;
	uint8_t masterMask, slaveMask;
}PIC8259;

static void pic8259_setPICMask(PIC *pic, enum IRQ irq, int setMask){
	PIC8259 *pic8259 = pic->pic8259;
	int i = (irq < 8? irq: irq - 8);
	uint8_t *mask = ((irq < 8)? &pic8259->masterMask: &pic8259->slaveMask);
	if(setMask)
		(*mask) |= (1 << i);
	else
		(*mask) &= (0xff ^ (1 << i));
	out8((irq < 8? M_OCW1: S_OCW1), *mask);
}

static InterruptVector *pic8259_irqToVector(PIC *pic, enum IRQ irq){
	PIC8259 *pic8259 = pic->pic8259;
	return getVector(pic8259->vectorBase, irq);
}

static void resetPIC8259(uint8_t vectorBase){
	// mask all
	out8(M_OCW1, 0xff);
	out8(S_OCW1, 0xff);
	// master
	out8(M_ICW1, 0x11);
	out8(M_ICW2, vectorBase);
	out8(M_ICW3, 1<<2);
	out8(M_ICW4, 1);
	// slave
	out8(S_ICW1, 0x11);
	out8(S_ICW2, vectorBase + 8);
	out8(S_ICW3, 2);
	out8(S_ICW4, 1);

	out8(M_OCW1, 0xff);
	out8(S_OCW1, 0xff);
}

void disablePIC8259(void){
	resetPIC8259(0);
}

static void pic8259_interruptAllOther(
	__attribute__((__unused__)) struct InterruptController *pic,
	__attribute__((__unused__)) InterruptVector *vector
){
}

PIC8259 *initPIC8259(InterruptTable *t){
	PIC8259 *NEW(pic);
	pic->this.pic8259 = pic;
	pic->this.numberOfProcessors = 1;
	pic->this.endOfInterrupt = pic8259_endOfInterrupt;
	pic->this.irqToVector = pic8259_irqToVector;
	pic->this.setPICMask = pic8259_setPICMask;
	pic->this.interruptAllOther = pic8259_interruptAllOther;

	pic->interruptTable = t;
	pic->vectorBase = registerIRQs(t, 0, 16);
	pic->masterMask = 0xff;
	pic->slaveMask = 0xff;
	resetPIC8259(toChar(pic->vectorBase));
	pic->this.setPICMask(&pic->this, SLAVE_IRQ, 0);

	printk("8259 interrupt #0 mapped to vector %d\n", toChar(pic->vectorBase));
	return pic;
}

PIC *castPIC8259(PIC8259 *pic8259){
	return &pic8259->this;
}
