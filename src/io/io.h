
typedef union PIC PIC;

struct InterruptVector;
void initTimer(PIC *vectorBase);
#define TIMER_FREQUENCY (100)
void setTimerFrequency(unsigned frequency);
