
typedef struct MemoryManager MemoryManager;
typedef struct InterruptTable InterruptTable;
typedef struct SegmentSelector SegmentSelector;
InterruptTable *initInterruptTable(MemoryManager *m, SegmentSelector *kernelCodeSelector);
void lidt(InterruptTable *t);
