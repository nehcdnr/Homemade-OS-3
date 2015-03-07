
typedef struct MemoryManager MemoryManager;
typedef struct InterruptTable InterruptTable;
typedef struct SegmentSelector SegmentSelector;
typedef struct ProcessorLocal ProcessorLocal;
InterruptTable *initInterruptTable(MemoryManager *m, SegmentSelector *kernelCodeSelector, ProcessorLocal *pl);
void lidt(InterruptTable *t);
