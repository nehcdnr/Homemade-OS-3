#include<std.h>

typedef struct SegmentTable SegmentTable;

typedef union{
	uint16_t value;
	struct{
		uint16_t rpl: 2;
		uint16_t ldt: 1;
		uint16_t index: 13;
	}bit;
}SegmentSelector;

enum SegmentIndex{
	GDT_0 = 0,
	GDT_KERNEL_CODE_INDEX,
	GDT_KERNEL_DATA_INDEX,
	GDT_USER_CODE_INDEX,
	GDT_USER_DATA_INDEX,
	GDT_TSS_INDEX,
	GDT_LENGTH
};
SegmentSelector getSegmentSelector(SegmentTable *t, enum SegmentIndex i);

SegmentTable *createSegmentTable(void);
SegmentSelector *getKernelCodeSelector(SegmentTable *t);
void setTSSKernelStack(SegmentTable *t, uint32_t esp0);
void loadgdt(SegmentTable *gdt);
void sgdt(uint32_t *base, uint16_t *limit);
