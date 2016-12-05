#ifndef IO_SERVICE_H_INCLUDED
#define IO_SERVICE_H_INCLUDED

#include"interrupt/handler.h"
#include"multiprocessor/processorlocal.h"
#include"io.h"

typedef struct IORequest IORequest;
typedef void CancelIO(void *instance);
typedef int AcceptIO(void *instance, uintptr_t *returnValues);
typedef struct Task Task;
struct IORequest{
	void *instance;
	// for Task.pendingIOList; see taskmanager.c
	IORequest **prev, *next;
	Task *task;
	// the request can be pending or completed
	// instance and IORequest should be deleted in this function
	CancelIO *cancel;
	// cancellable is the critical section flag shared by the threads calling completeIO and cancelIO
	int cancellable;
	// IMPROVE: status = PENDING, CANCELLABLE, CANCELLING, COMPLETED
	// return number of elements in returnValues
	// instance and IORequest should be deleted in this function
	AcceptIO *accept;
	// in the above 3 functions and initIORequest,
	// initIORequest(), cancel() and accept() are always invoked by its own task;
	// IO may be handled by different task.
	// handling and cancel() may run concurrently.
};
void pendIO(IORequest *ior);
IORequest *waitAnyIO(void);
void waitIO(IORequest *expected);
int tryCancelIO(IORequest *ior);
void completeIO(IORequest *ior); // IORequestHandler

int isCancellable(IORequest *ior);
// if value == 0 and ior is being cancelled (see taskmanager.c), return 0
// if value != 0 or ior is not being cancelled, return 1
int setCancellable(IORequest *ior, int value);

CancelIO notSupportCancelIO;

void initIORequest(
	IORequest *ior,
	void *instance,
	//Task* t,
	CancelIO *cancelIO,
	AcceptIO *acceptIO
);

// timer8254.c
void setTimer8254Frequency(unsigned frequency);
// interval in milliseconds
void setTimer8254OneShot(unsigned interval);
uint16_t readTimer8254Count(void);

// timer.c
typedef struct InterruptParam InterruptParam;
typedef struct TimerEventList TimerEventList;
typedef struct InterruptVector InterruptVector;
#define TIMER_FREQUENCY (100)
TimerEventList *createTimer(void);

// for LAPIC timer
void setTimerHandler(TimerEventList *tel, InterruptVector *v);
// for IRQ timer
int addTimerHandler(TimerEventList *tel, InterruptVector *v);
typedef struct SystemCallTable SystemCallTable;
void initTimer(SystemCallTable *systemCallTable);

// cmos.c
void initCMOS(PIC *pic, SystemCallTable *s);

//console.h
void initKernelConsole(void);
void kernelConsoleService(void);

// video.c
void vbeDriver(void);

// keyboard.c
void ps2Driver(void);

// pci.c
void pciDriver(void);

#define PCI_COMMON_CONFIG_REGISTERS \
struct{\
	uint16_t vendorID, deviceID;\
	uint16_t command, status;\
	union{\
		struct{uint8_t revision, programInterface, subclassCode, classCode;};\
		uint32_t classCodes;\
	};\
	union{\
		struct{uint8_t cacheLineSize, latencyTimer, headerType, bist;};\
		uint32_t types;\
	};\
}

typedef struct{
	PCI_COMMON_CONFIG_REGISTERS;
}PCICommonConfigRegisters;

// for headerType = 0x00
typedef struct{
	PCI_COMMON_CONFIG_REGISTERS;
	uint32_t bar0;
	uint32_t bar1;
	uint32_t bar2;
	uint32_t bar3;
	uint32_t bar4;
	uint32_t bar5;
	uint32_t cardbus;
	uint16_t subsystemVendorID, subsystemID;
	uint32_t expansionROM;
	uint8_t  capability, reserved0[3];
	uint32_t reserved1;
	uint8_t interruptLine, interruptPIN, minGrant, maxLatency;
}PCIConfigRegisters0;

typedef struct{
	PCI_COMMON_CONFIG_REGISTERS;
	uint32_t bar0;
	uint32_t bar1;
	uint8_t primaryBus, secondaryBus, subordinateBus, secondaryTimer;
	uint8_t ioBase, ioLimit;
	uint16_t secondaryStatus;
	uint16_t memoryBase, memoryLimit;
	uint16_t prefetchMemoryBase, prefetchMemoryLimit;
	uint32_t prefetchBaseHigh;
	uint32_t prefetchBaseLow;
	uint16_t ioBaseHigh;
	uint16_t ioLimitHigh;
	uint8_t capability, reserved0[3];
	uint32_t expansionROM;
	uint8_t interruptLine, interruptPIN;
	uint16_t bridgeControl;
}PCIConfigRegisters1;

typedef struct{
	PCI_COMMON_CONFIG_REGISTERS;
	uint8_t unimplemented[0x38];
}PCIConfigRegisters2;


typedef union{
	 PCICommonConfigRegisters common;
	 PCIConfigRegisters0 regs0;
	 PCIConfigRegisters0 regs1;
	 PCIConfigRegisters0 regs2;
}PCIConfigRegisters;

// return a file enumeration handle
// the enumeration consists of PCI paths (bus, deivce, function)
// whose class code & classMask == classCode & classMask
uintptr_t enumeratePCI(uint32_t classCode, uint32_t classMask);

uintptr_t nextPCIConfigRegisters(uintptr_t pciEnumHandle, PCIConfigRegisters *regs, uintptr_t readSize);

// ahci.c
void ahciDriver(void);

// intel8254x.c
void i8254xDriver(void);

// network/internet.c
void internetService(void);

#endif
