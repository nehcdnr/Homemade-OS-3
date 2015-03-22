#ifndef INTERRUPT_H_INCLUDED
#define INTERRUPT_H_INCLUDED
#include<std.h>

typedef struct InterruptParam InterruptParam;

typedef struct MemoryManager MemoryManager;
typedef struct InterruptTable InterruptTable;
typedef struct SegmentTable SegmentTable;
typedef struct ProcessorLocal ProcessorLocal;
InterruptTable *initInterruptTable(MemoryManager *m, SegmentTable *gdt, ProcessorLocal *pl);
void callHandler(InterruptTable *t, uint8_t intNumber, InterruptParam *p);
void lidt(InterruptTable *t);

#endif
