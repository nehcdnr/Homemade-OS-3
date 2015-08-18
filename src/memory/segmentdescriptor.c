#include"common.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"segment.h"

typedef struct{
	uint16_t limit_0_16;
	uint16_t base_0_16;
	uint8_t base_16_24;
	uint8_t access;
	uint8_t limit_16_20: 4;
	uint8_t flag: 4;
	uint8_t base_24_32;
}SegmentDescriptor;

typedef struct{
	uint16_t previousTaskLink, reserved0;
	uint32_t esp0, ss0, esp1, ss1, esp2, ss2;
	uint32_t cr3, eip, eflags,
	eax, ecx, edx, ebx, esp, ebp, esi, edi;
	uint32_t es, cs, ss, ds, fs, gs, ldtSelector;
	uint16_t
	debugTrap: 1,
	reserved11: 15,
	ioBitmapAddress;
}TSS;

static_assert(sizeof(TSS) == 104);

struct SegmentSelector{
	uint16_t shortValue;
};

uint16_t toShort(SegmentSelector* s){
	return s->shortValue;
}

struct SegmentTable{
	int length;
	TSS *tss;
	SegmentSelector *selector;
	SegmentDescriptor *descriptor;
};

enum SegmentType{
	KERNEL_CODE = 0x98,
	KERNEL_DATA = 0x92,
	// USER_CODE = ,
	// USER_DATA =
	KERNEL_TSS = 0x89
};

static SegmentSelector *setSegment(
	SegmentTable*t,
	int index,
	uint32_t base,
	uint32_t limit,
	enum SegmentType type
){
	uint8_t flag;
	if(limit >= (1 << 20)){
		assert(limit % 4096 == 4095);
		limit = (limit / 4096);
		flag = 8 + 4;
	}
	else{
		flag = 4;
	}

	SegmentDescriptor *d = &(t->descriptor[index]);
	d->limit_0_16 = (limit & 65535);
	d->base_0_16 = (base & 65535);
	d->base_16_24 = ((base >> 16) & 255);
	d->access = type;
	d->limit_16_20 = ((limit >> 16) & 15);
	d->flag =  flag;
	d->base_24_32 = ((base >> 24) & 255);
	t->selector[index].shortValue = (index << 3)/* + privilege*/;
	return t->selector + index;
}

static_assert(sizeof(SegmentDescriptor) == 8);

enum{
	GDT_0 = 0,
	GDT_KERNEL_CODE_INDEX,
	GDT_KERNEL_DATA_INDEX,
	GDT_TSS_INDEX,
	GDT_LENGTH
};
SegmentTable *createSegmentTable(void){
	const int length = GDT_LENGTH;
	SegmentTable *NEW(t);
	NEW_ARRAY(t->selector, length);
	{
		uintptr_t desc = (uintptr_t)allocateKernelMemory((length + 1) * sizeof(SegmentDescriptor));
		while(desc % sizeof(SegmentDescriptor) != 0){
			desc++;
		}
		t->descriptor = (SegmentDescriptor*)desc;
	}
	t->length = length;
	MEMSET0(t->descriptor + 0);
	setSegment(t, GDT_KERNEL_CODE_INDEX, 0, 0xffffffff, KERNEL_CODE);
	setSegment(t, GDT_KERNEL_DATA_INDEX, 0, 0xffffffff, KERNEL_DATA);
	{
		TSS *NEW(tss);
		MEMSET0(tss);
		tss->ss0 = toShort(t->selector + GDT_KERNEL_DATA_INDEX);
		tss->esp0 = 0;
		tss->ioBitmapAddress = 0xffff; // sizeof(TSS);
		t->tss = tss;
	}
	setSegment(t, GDT_TSS_INDEX, (uintptr_t)t->tss, sizeof(TSS) - 1, KERNEL_TSS);
	return t;
}

SegmentSelector *getKernelCodeSelector(SegmentTable *t){
	return t->selector + GDT_KERNEL_CODE_INDEX;
}
/*
SegmentSelector *getKernelDataSelector(SegmentTable *t){
	return t->selector + GDT_KERNEL_DATA_INDEX;
}
*/
void setTSSKernelStack(SegmentTable *t, uint32_t esp0){
	t->tss->ss0 = toShort(t->selector + GDT_KERNEL_DATA_INDEX);
	t->tss->esp0 = esp0;
}

static void ltr(uint16_t tssSelector){
	__asm__(
	"ltr %0\n"
	:
	:"a"(tssSelector)
	);
}

// assembly.asm
void lgdt(
	uint32_t limit,
	SegmentDescriptor *base,
	uint16_t codeSegment,
	uint16_t dataSegment
);

void loadgdt(SegmentTable *gdt){
	lgdt(
		gdt->length * sizeof(SegmentDescriptor) - 1,
		gdt->descriptor,
		gdt->selector[GDT_KERNEL_CODE_INDEX].shortValue,
		gdt->selector[GDT_KERNEL_DATA_INDEX].shortValue
	);
	ltr(gdt->selector[GDT_TSS_INDEX].shortValue);
}

void sgdt(uint32_t *base, uint16_t *limit){
	uint16_t sgdt[3];
	__asm__(
	"sgdtl %0\n"
	:"=m"(sgdt)
	:
	);
	*limit = sgdt[0];
	*base = sgdt[1] + (((uint32_t)sgdt[2]) << 16);
}
