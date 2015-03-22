#include"handler.h"
#include"interrupt.h"
#include"internalinterrupt.h"
#include"multiprocessor/processorlocal.h"
#include"common.h"

// return 1 if ok
static int v8086Push(InterruptParam *p, uint16_t v){
	if((p->esp & 0xffff) < 2){
		panic("v8086 stack pointer underflow");
		return 0;
	}
	p->esp -= 2;
	uint16_t *s = (uint16_t*)(((p->ss & 0xffff) << 4) + (p->esp & 0xffff));
	*s = v;
	return 1;
}

static int v8086Pop(InterruptParam *p, uint16_t *v){
	if((p->esp & 0xffff) >= 0x10000 - 2){
		panic("v8086 stack pointer overflow"); //TODO: terminate
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
	uint8_t *instruction = (uint8_t*)(((p->cs & 0xffff) << 4) + (p->eip & 0xffff));
	switch(instruction[0]){
	case 0xcf: // iret
		p->eip += 1;
		v8086Pop(p, (uint16_t*)&p->eip);
		v8086Pop(p, (uint16_t*)&p->cs);
		v8086Pop(p, (uint16_t*)&p->eflags.value);
		break;
	case 0xfa: // cli
		p->eip += 1;
		p->eflags.bit.interrupt = 0;
		break;
	case 0x9c: // pushf
		p->eip += 1;
		v8086Push(p, p->eflags.value);
		break;
	case 0x9d: // popf
		p->eip += 1;
		v8086Pop(p, (uint16_t*)&p->eflags.value);
		break;
	case 0xec: // in al, dx
		p->eip += 1;
		p->regs.eax = in8(p->regs.edx & 0xffff);
		break;
	case 0xee: // out dx, al
		p->eip += 1;
		out8((p->regs.edx & 0xffff), (p->regs.eax & 0xff));
		break;
	case 0xef: // out dx, ax
		p->eip += 1;
		out16((p->regs.edx & 0xffff), (p->regs.eax & 0xffff));
		break;
	case 0xcd: // int BYTE n
		p->eip += 2;
		if(instruction[1] == 0x10){ // BIOS
			const uint16_t (*const v8086IntTable)[2] = ((uint16_t)0);
			v8086Push(p, p->eflags.value);
			v8086Push(p, p->cs);
			v8086Push(p, p->eip);
			p->eip = v8086IntTable[instruction[1]][0];
			p->cs = v8086IntTable[instruction[1]][1];
		}
		else if(instruction[1] == SYSTEM_CALL){
			callHandler(p->processorLocal->idt, instruction[1], p);
		}
		else{
			printk("int %d ", instruction[1]);
			panic("unhandled virtual 8086 interrupt");
		}
		break;
	default:
		printk("%x %x\n", instruction[0],p->eflags.value);
		panic("unhandled intruction in v8086 mode");
	}
}

static void generalProtectionHandler(InterruptParam *p){
	// printk("%x %x %x %x %x %x\n",p->eip,p->cs,p->regs.eax,p->errorCode,p->ss,p->esp);
	if(p->eflags.bit.virtual8086){
		v8086Monitor(p);
	}
	else{
		printk("general protection: %d, cs = %x, eip = %x\n",p->errorCode, p->cs, p->eip);
		assert(0);
		sti();
	}
}

void initInternalInterrupt(InterruptTable *idt){
	registerInterrupt(idt, GENRAL_PROTECTION_FAULT, generalProtectionHandler, 0);
}
