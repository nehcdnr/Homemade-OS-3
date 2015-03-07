#include"common.h"
#include"memory/memory.h"
#include"segment/segment.h"

typedef struct{
	uint16_t limit_0_16;
	uint16_t base_0_16;
	uint8_t base_16_24;
	uint8_t access;
	uint8_t limit_16_20: 4;
	uint8_t flag: 4;
	uint8_t base_24_32;
}SegmentDescriptor;

struct SegmentSelector{
	uint16_t shortValue;
};

uint16_t toShort(SegmentSelector* s){
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
	SegmentTable *NEW(t, m);
	NEW(t->selector, m);
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
	uint32_t limit,
	SegmentDescriptor *base,
	uint16_t codeSegment,
	uint16_t dataSegment
);

void loadgdt(SegmentTable *gdt, SegmentSelector *codeSegment, SegmentSelector *dataSegment){
	lgdt(gdt->usedCount * sizeof(SegmentDescriptor) - 1, gdt->descriptor,
	codeSegment->shortValue, dataSegment->shortValue);
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
