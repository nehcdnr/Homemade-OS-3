#include"handler.h"
#include"interrupt.h"
#include"internalinterrupt.h"
#include"multiprocessor/processorlocal.h"
#include"common.h"

// return 1 if ok
static int v8086Push(InterruptParam *p, uint16_t v){
	if((p->esp & 0xffff) < 2){
		printk("v8086 stack pointer underflow\n");
		 // see generalProtectionHandler()
		defaultInterruptHandler(p);
		return 0;
	}
	p->esp -= 2;
	uint16_t *s = (uint16_t*)(((p->ss & 0xffff) << 4) + (p->esp & 0xffff));
	*s = v;
	return 1;
}

static int v8086Pop(InterruptParam *p, uint16_t *v){
	if((p->esp & 0xffff) >= 0x10000 - 2){
		printk("v8086 stack pointer overflow\n");
		 // see generalProtectionHandler()
		defaultInterruptHandler(p);
		return 0;
	}
	uint16_t *s = (uint16_t*)(((p->ss & 0xffff) << 4) + (p->esp & 0xffff));
	*v = *s;
	p->esp += 2;
	return 1;
}

static void v8086Monitor(InterruptParam *p){
	// CLI, STI, PUSHF, POPF, INT , IRET
	// 0xfa, 0xfb, 0x9c, 0x9d, {0xcc, 0xcd imm8, 0xce}, 0xcf
	// IN, INS, OUT, OUTS
	int operandSize32Flag = 0;
	uint8_t *instruction = (uint8_t*)(((p->cs & 0xffff) << 4) + (p->eip & 0xffff));
	p->eip++;
	if(instruction[0] == 0x66){ // operand size prefix
		operandSize32Flag = 1;
		instruction++;
		assert(instruction[0] == 0xed || instruction[0] == 0xef);
		p->eip++;
	}
	switch(instruction[0]){
	case 0xcf: // iret
		v8086Pop(p, (uint16_t*)&p->eip);
		v8086Pop(p, (uint16_t*)&p->cs);
		v8086Pop(p, (uint16_t*)&p->eflags.value);
		break;
	case 0xfa: // cli
		p->eflags.bit.interrupt = 0;
		break;
	case 0x9c: // pushf
		v8086Push(p, p->eflags.value);
		break;
	case 0x9d: // popf
		v8086Pop(p, (uint16_t*)&p->eflags.value);
		break;
	case 0xec: // in al, dx
		p->regs.eax = in8(p->regs.edx & 0xffff);
		break;
	case 0xed: // in ax, dx
		if(operandSize32Flag){
			p->regs.eax = in32(p->regs.edx & 0xffff);
		}
		else{
			p->regs.eax = in16(p->regs.edx & 0xffff);
		}
		break;
	case 0xee: // out dx, al
		out8((p->regs.edx & 0xffff), (p->regs.eax & 0xff));
		break;
	case 0xef: // out dx, ax
		if(operandSize32Flag){
			out32((p->regs.edx & 0xffff), p->regs.eax);
		}
		else{
			out16((p->regs.edx & 0xffff), (p->regs.eax & 0xffff));
		}
		break;
	case 0xcd: // int BYTE n
		p->eip++;
		if(instruction[1] == 0x10){ // BIOS
			const uint16_t (*const v8086IntTable)[2] = 0;
			v8086Push(p, p->eflags.value);
			v8086Push(p, p->cs);
			v8086Push(p, p->eip);
			p->eip = v8086IntTable[instruction[1]][0];
			p->cs = v8086IntTable[instruction[1]][1];
		}
		else if(instruction[1] == SYSTEM_CALL){
			callHandler(global.idt, instruction[1], p);
		}
		else{
			printk("int %d ", instruction[1]);
			panic("unhandled virtual 8086 interrupt");
		}
		break;
	default:
		printk("instruction %x, eflags = %x\n", instruction[0] ,p->eflags.value);
		panic("unhandled intruction in v8086 mode");
	}
}

static void generalProtectionHandler(InterruptParam *p){
	if(p->eflags.bit.virtual8086){
		sti();
		v8086Monitor(p);
	}
	else{
		// terminate
		defaultInterruptHandler(p);
		assert(0);
	}
}

static void pageFaultHandler(InterruptParam *p){
	printk("page fault: CR0 = %x CR2 = %x CR3 = %x\n", getCR0(), getCR2(), getCR3());
	defaultInterruptHandler(p);
}

void initInternalInterrupt(InterruptTable *idt){
	registerInterrupt(idt, GENERAL_PROTECTION_FAULT, generalProtectionHandler, 0);
	registerInterrupt(idt, PAGE_FAULT, pageFaultHandler, 0);
}
