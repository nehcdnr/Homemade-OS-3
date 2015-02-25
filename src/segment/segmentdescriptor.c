#include"common.h"
#include"memory/memory.h"
#include"segment/segment.h"

typedef struct{
	unsigned short limit_0_16;
	unsigned short base_0_16;
	unsigned char base_16_24;
	unsigned char access;
	unsigned char limit_16_20: 4;
	unsigned char flag: 4;
	unsigned char base_24_32;
}SegmentDescriptor;

struct SegmentSelector{
	unsigned short shortValue;
};

unsigned short toShort(SegmentSelector* s){
	return s->shortValue;
}

static_assert(sizeof(SegmentDescriptor) == 8);

struct SegmentTable{
	int length;
	int usedCount;
	SegmentSelector *selector;
	SegmentDescriptor *descriptor;
};

SegmentTable *createSegmentTable(MemoryManager *m, int length){
	SegmentTable *t = allocate(m, sizeof(SegmentTable));
	t->selector = allocate(m, length * sizeof(SegmentSelector));
	t->descriptor = allocateAligned(m, length * sizeof(SegmentDescriptor), sizeof(SegmentDescriptor));
	t->length = length;
	t->usedCount = 0;
	return t;
}

void setSegment0(SegmentTable *t){
	SegmentDescriptor *d = &(t->descriptor[0]);
	d->limit_0_16 = 0;
	d->base_0_16 = 0;
	d->base_16_24 = 0;
	d->access = 0;
	d->limit_16_20 = 0;
	d->flag = 0;
	d->base_24_32 = 0;
	if(t->usedCount == 0){
		t->usedCount++;
	}
}

SegmentSelector *addSegment(
	SegmentTable*t,
	unsigned base,
	unsigned limit,
	enum SegmentType type
){
	unsigned char flag;
	if(limit >= (1 << 20)){
		assert(limit % 4096 == 4095);
		limit = (limit / 4096);
		flag = 8 + 4;
	}
	else{
		flag = 8;
	}
	const int u = t->usedCount;
	assert(u < t->length);
	t->usedCount++;

	SegmentDescriptor *d = &(t->descriptor[u]);
	d->limit_0_16 = (limit & 65535);
	d->base_0_16 = (base & 65535);
	d->base_16_24 = ((base >> 16) & 255);
	d->access = type;
	d->limit_16_20 = ((limit >> 16) & 15);
	d->flag =  flag;
	d->base_24_32 = ((base >> 24) & 255);
	t->selector[u].shortValue = (u << 3);
	return t->selector + u;
}

// assembly.asm
void lgdt(
	unsigned limit,
	SegmentDescriptor *base,
	unsigned short codeSegment,
	unsigned short dataSegment
);

void loadgdt(SegmentTable *gdt, SegmentSelector *codeSegment, SegmentSelector *dataSegment){
	lgdt(gdt->usedCount * sizeof(SegmentDescriptor) - 1, gdt->descriptor,
	codeSegment->shortValue, dataSegment->shortValue);
}

void sgdt(unsigned int *base, unsigned short *limit){
	unsigned short sgdt[3];
	__asm__(
	"sgdtl %0\n"
	:"=m"(sgdt)
	:
	);
	*limit = sgdt[0];
	*base = sgdt[1] + (((unsigned int)sgdt[2]) << 16);
}
