#include"interrupt/handler.h"

typedef struct IORequest IORequest;
struct IORequest{
	union{
		void *ioRequest;
		struct TimerEvent *timerEvent;
	};
	// for Task.pendingIOList; see taskmanager.c
	IORequest **prev,*next;

	void (*handle)(uintptr_t arg);
	uintptr_t arg;
	void (*cancel)(IORequest*);
	void (*destroy)(IORequest*);
};

void initIORequest(
	IORequest *this,
	void *instance,
	void (*h)(uintptr_t),
	uintptr_t a,
	void (*c)(struct IORequest*),
	void (*d)(struct IORequest*)
);

// timer8254.c
void setTimer8254Frequency(unsigned frequency);

// timer.c
typedef struct InterruptParam InterruptParam;
typedef struct TimerEventList TimerEventList;
typedef struct InterruptVector InterruptVector;
#define TIMER_FREQUENCY (100)
TimerEventList *createTimer(void);

typedef struct TimerEvent TimerEvent;
IORequest *addTimerEvent(
	TimerEventList* tel, uint64_t waitTicks,
	void (*callback)(uintptr_t), uintptr_t arg
);
void setTimerHandler(TimerEventList *tel, InterruptVector *v);
void initTimer(void);

//console.h
void initKernelConsole(void);
void kernelConsoleService(void);

// video.c
void vbeDriver(void);

// keyboard.c
void ps2Driver(void);

// pci.c
void pciDriver(void);
