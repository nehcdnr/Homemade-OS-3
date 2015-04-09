#ifndef INTERRUPT_H_INCLUDED
#define INTERRUPT_H_INCLUDED
#include<std.h>

typedef struct InterruptParam InterruptParam;

typedef struct InterruptTable InterruptTable;
typedef struct SegmentTable SegmentTable;
InterruptTable *initInterruptTable(SegmentTable *gdt);
void callHandler(InterruptTable *t, uint8_t intNumber, InterruptParam *p);
void lidt(InterruptTable *t);

#endif
