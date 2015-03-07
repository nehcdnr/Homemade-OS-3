// timer8254.c
typedef union PIC PIC;
void setTimer8254Frequency(unsigned frequency);

// timer.c
typedef struct InterruptParam InterruptParam;
typedef struct TimerEventList TimerEventList;
typedef struct MemoryManager MemoryManager;
typedef struct InterruptVector InterruptVector;
#define TIMER_FREQUENCY (100)
TimerEventList *createTimer(MemoryManager *m);
void replaceTimerHandler(TimerEventList *tel, InterruptVector *v);
void kernelSleep(TimerEventList *tel, unsigned millisecond);

// video.c
typedef struct ConsoleDisplay ConsoleDisplay;
int printConsole(ConsoleDisplay *cd, const char *s);
ConsoleDisplay *initKernelConsole(MemoryManager *m);

// keyboard.c
void replaceKeyboardHandler(InterruptVector *v);
void replaceMouseHandler(InterruptVector *v);
