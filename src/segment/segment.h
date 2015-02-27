#include<std.h>

typedef struct SegmentTable SegmentTable;
typedef struct SegmentSelector SegmentSelector;
uint16_t toShort(SegmentSelector* s);

enum SegmentType{
	KERNEL_CODE = 0x98,
	KERNEL_DATA = 0x92,
	/*
	USER_CODE = ,
	USER_DATA =
	*/
	KERNEL_TSS = 0x89
};

typedef struct TaskStateSegment TSS;

SegmentTable *createSegmentTable(MemoryManager *m, int length);
void setSegment0(SegmentTable*t);
SegmentSelector *addSegment(SegmentTable *t, uint32_t base, uint32_t limit, enum SegmentType type);
SegmentSelector *addTSS(SegmentTable *t, TSS *tss);
void loadgdt(SegmentTable *gdt, SegmentSelector *codeSegment, SegmentSelector *dataSegment);
void sgdt(uint32_t *base, uint16_t *limit);
