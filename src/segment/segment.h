#include<std.h>

typedef struct SegmentTable SegmentTable;
typedef struct SegmentSelector SegmentSelector;
uint16_t toShort(SegmentSelector* s);

SegmentTable *createSegmentTable(void);
SegmentSelector *getKernelCodeSelector(SegmentTable *t);
void setTSSKernelStack(SegmentTable *t, uint32_t esp0);
void loadgdt(SegmentTable *gdt);
void sgdt(uint32_t *base, uint16_t *limit);
