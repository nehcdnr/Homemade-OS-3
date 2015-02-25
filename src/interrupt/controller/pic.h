typedef struct MemoryManager MemoryManager;
typedef struct InterruptTable InterruptTable;
typedef union PIC PIC;
// use PIC8259 or APIC
PIC *initPIC(MemoryManager *m, InterruptTable *t);
