// timer8254.c
void setTimer8254Frequency(unsigned frequency);

// timer.c
typedef struct InterruptParam InterruptParam;
typedef struct TimerEventList TimerEventList;
typedef struct InterruptVector InterruptVector;
#define TIMER_FREQUENCY (100)
TimerEventList *createTimer(void);
void replaceTimerHandler(TimerEventList *tel, InterruptVector *v);
void kernelSleep(TimerEventList *tel, unsigned millisecond);

// video.c
typedef struct ConsoleDisplay ConsoleDisplay;
int printConsole(ConsoleDisplay *cd, const char *s);
ConsoleDisplay *initKernelConsole(void);

// keyboard.c
typedef struct InterruptController PIC;
typedef struct SystemCallTable SystemCallTable;
void initPS2Driver(PIC *pic, SystemCallTable *syscallTable);
