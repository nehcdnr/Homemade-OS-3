#include"pic.h"
#include"pic_private.h"
#include"interrupt/interrupt.h"
#include"memory/memory.h"
#include"common.h"
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

static void eoi8259(InterruptVector *v){
	int irq = getIRQ(v);
	assert(irq >= 0 && irq < 16);
	if(irq >= 8)
		out(S_OCW2, 0x20);
	out(M_OCW2, 0x20);
}

typedef struct PIC8259{
	PIC this;
	InterruptTable *interruptTable;
	InterruptVector *vectorBase;
	uint8_t masterMask, slaveMask;
}PIC8259;

static void setPIC8259Mask(PIC *pic, enum IRQ irq, int setMask){
	PIC8259 *pic8259 = pic->pic8259;
	int i = (irq < 8? irq: irq - 8);
	uint8_t *mask = (irq < 8? &pic8259->masterMask: &pic8259->slaveMask);
	if(setMask)
		(*mask) |= (1 << i);
	else
		(*mask) &= (0xff ^ (1 << i));
	out((irq < 8? M_OCW1: S_OCW1), *mask);
}

static InterruptVector *PIC8259IRQToVector(PIC *pic, enum IRQ irq){
	PIC8259 *pic8259 = pic->pic8259;
	return getVector(pic8259->vectorBase, irq);
}

static void resetPIC8259(uint8_t vectorBase){
	// mask all
	out(M_OCW1, 0xff);
	out(S_OCW1, 0xff);
	// master
	out(M_ICW1, 0x11);
	out(M_ICW2, vectorBase);
	out(M_ICW3, 1<<2);
	out(M_ICW4, 1);
	// slave
	out(S_ICW1, 0x11);
	out(S_ICW2, vectorBase + 8);
	out(S_ICW3, 2);
	out(S_ICW4, 1);

	out(M_OCW1, 0xff);
	out(S_OCW1, 0xff);
}

void disablePIC8259(void){
	resetPIC8259(0);
}

PIC8259 *initPIC8259(struct MemoryManager *m, InterruptTable *t){
	PIC8259 *NEW(pic, m);
	pic->this.pic8259 = pic;
	pic->interruptTable = t;
	pic->vectorBase = registerIRQs(t, 0, 16);
	pic->masterMask = 0xff;
	pic->slaveMask = 0xff;
	resetPIC8259(toChar(pic->vectorBase));
	irqToVector = PIC8259IRQToVector;
	setPICMask = setPIC8259Mask;

	endOfInterrupt = eoi8259;
	kprintf("8259 interrupt #0 mapped to vector %d\n", toChar(pic->vectorBase));
	return pic;
}

PIC *castPIC8259(PIC8259 *pic8259){
	return &pic8259->this;
}
