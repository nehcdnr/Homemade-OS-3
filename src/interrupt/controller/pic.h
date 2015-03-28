#include<std.h>

typedef struct InterruptTable InterruptTable;
typedef union PIC PIC;
typedef struct TimerEventList TimerEventList;
typedef struct ProcessorLocal ProcessorLocal;
// use PIC8259 or APIC
PIC *initPIC(InterruptTable *t, TimerEventList *timer, ProcessorLocal *pl);
