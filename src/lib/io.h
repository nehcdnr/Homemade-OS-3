#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#define IO_REQUEST_FAILURE ((uintptr_t)0)

uintptr_t systemCall_setAlarm(uint64_t millisecond, int isPeriodic);
int sleep(uint64_t millisecond);

// call with UINTPTR_NULL to wait for any I/O request
// IMPROVE: struct IORequestHandle{uintptr_t value;};
uintptr_t systemCall_waitIO(uintptr_t ioNumber);
uintptr_t systemCall_waitIOReturn(uintptr_t ioNumber, int returnCount, ...);
int systemCall_cancelIO(uintptr_t io);
int cancelOrWaitIO(uintptr_t io);

#endif
