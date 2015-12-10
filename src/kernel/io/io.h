#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#include"interrupt/handler.h"

typedef struct IORequest IORequest;
typedef void CancelIORequest(IORequest*);
typedef int FinishIORequest(IORequest*, uintptr_t*);
typedef struct Task Task;
struct IORequest{
	void *instance;
	// for Task.pendingIOList; see taskmanager.c
	IORequest **prev, *next;
	Task *task;
	// the request can be pending or finished
	// IORequest should be deleted in this function
	CancelIORequest *cancel;
	int cancellable;
	// return number of elements in returnValues
	// IORequest should be deleted in this function
	FinishIORequest *finish;
	// in the above 3 functions and initIORequest,
	// initIORequest(), cancel() and finish() are always invoked by its own task;
	// handle() may be invoked by different task.
	// handle() and cancel() may run concurrently.
};
void pendIO(IORequest *ior);
IORequest *waitIO(Task *t, IORequest *ioNumber);
void finishIO(IORequest *ior); // IORequestHandler

// call with UINTPTR_NULL to wait for any I/O request
// IMPROVE: struct IORequestHandle{uintptr_t value;};
uintptr_t systemCall_waitIO(uintptr_t ioNumber);
uintptr_t systemCall_waitIOReturn(uintptr_t ioNumber, int returnCount, ...);
int systemCall_cancelIO(uintptr_t io);

void setCancellable(IORequest *ior, int value);

CancelIORequest notSupportCancelIORequest;

void initIORequest(
	IORequest *this,
	void *instance,
	//Task* t,
	CancelIORequest *cancelIORequest,
	FinishIORequest *finishIORequest
);

#define IO_REQUEST_FAILURE ((uintptr_t)0)

// timer8254.c
void setTimer8254Frequency(unsigned frequency);

// timer.c
typedef struct InterruptParam InterruptParam;
typedef struct TimerEventList TimerEventList;
typedef struct InterruptVector InterruptVector;
#define TIMER_FREQUENCY (100)
TimerEventList *createTimer(void);

uintptr_t systemCall_setAlarm(uint64_t millisecond, int isPeriodic);
int sleep(uint64_t millisecond);

void setTimerHandler(TimerEventList *tel, InterruptVector *v);
typedef struct SystemCallTable SystemCallTable;
void initTimer(SystemCallTable *systemCallTable);

//console.h
void initKernelConsole(void);
void kernelConsoleService(void);

// video.c
void vbeDriver(void);

// keyboard.c
void ps2Driver(void);
uintptr_t systemCall_readKeyboard(void);

// pci.c
void pciDriver(void);

// return index of found PCI configuration space
// return 0xffff if no element matches
// call this function with index + 1 to enumerate next one
uintptr_t systemCall_discoverPCI(
	uint32_t classCode, uint32_t classMask // parameters
);

enum PCIConfigRegister{
	// device id, vendor id
	// 16, 16
	DEVICE_VENDOR = 0x00,
	// status, command
	// 16, 16
	STATUS_COMMAND = 0x04,
	// class, subclass, program interface, revision id
	// 8, 8, 8, 8
	CLASS_REVISION = 0x08,
	// built-in self test, header type, latency timer, cache line size
	// 8, 8, 8 ,8
	HEADER_TYPE = 0x0c,
	// memory mapped registers
	// 32
	BASE_ADDRESS_0 = 0x10 + 0,
	BASE_ADDRESS_1 = 0x10 + 4,
	BASE_ADDRESS_2 = 0x10 + 8,
	BASE_ADDRESS_3 = 0x10 + 12,
	BASE_ADDRESS_4 = 0x10 + 16,
	BASE_ADDRESS_5 = 0x10 + 20,

	// for header type = 0x10
	// secondary Latency timer, subordinate bus number, secondary bus number, primary bus number
	// 8, 8, 8, 8
	// primary: direct upstream; secondary: direct downstream; subordinate: most downstream
	BUS_NUMBER = 0x18,

	// for USB host controller
	// serial bus release number
	// 8
	SBRN = 0x60,

	// for AHCI host controller
	// max latency, min grant, pin, interrupt irq
	// 8, 8, 8, 8
	INTERRUPT_INFORMATION = 0x3c
};

uint32_t readPCIConfig(uint8_t bus, uint8_t dev, uint8_t func, enum PCIConfigRegister reg);

// ahci.c
void ahciDriver(void);

// return 0 if fail
// return 1 if the io request is issued
uintptr_t systemCall_rwAHCI(uint32_t buffer, uint64_t lba, uint32_t sectorCount, uint32_t index, int isWrite);

#endif
