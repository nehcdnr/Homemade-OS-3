#include"systemcall.h"
#include"io.h"
#include"common.h"

uintptr_t systemCall_waitIO(uintptr_t ioNumber){
	return systemCall2(SYSCALL_WAIT_IO, ioNumber);
}

uintptr_t systemCall_waitIOReturn(uintptr_t ioNumber, int returnCount, ...){
	if(returnCount < 0 || returnCount >= SYSTEM_CALL_MAX_RETURN_COUNT){
		return IO_REQUEST_FAILURE;
	}
	va_list va;
	va_start(va, returnCount);
	uintptr_t ignoredReturnValue = 0;
	uintptr_t *returnValues[SYSTEM_CALL_MAX_RETURN_COUNT - 1];
	int i;
	for(i = 0; i < returnCount; i++){
		returnValues[i] = va_arg(va, uintptr_t*);
	}
	for(i = returnCount; i < (int)LENGTH_OF(returnValues); i++){
		returnValues[i] = &ignoredReturnValue;
	}
	(*returnValues[0]) = ioNumber;
	uintptr_t rv0 = systemCall6Return(
		SYSCALL_WAIT_IO,
		returnValues[0],
		returnValues[1],
		returnValues[2],
		returnValues[3],
		returnValues[4]
	);
	va_end(va);
	return rv0;
}

int systemCall_cancelIO(uintptr_t io){
	return (int)systemCall2(SYSCALL_CANCEL_IO, io);
}

int cancelOrWaitIO(uintptr_t io){
	int cancelled = systemCall_cancelIO(io);
	if(cancelled){
		return io;
	}
	return systemCall_waitIO(io) == io;
}

uintptr_t systemCall_setAlarm(uint64_t millisecond, int isPeriodic){
	return systemCall4(SYSCALL_SET_ALARM, LOW64(millisecond), HIGH64(millisecond), (uintptr_t)isPeriodic);
}

int sleep(uint64_t millisecond){
	uintptr_t te = systemCall_setAlarm(millisecond, 0);
	if(te == IO_REQUEST_FAILURE){
		return 0;
	}
	uintptr_t te2 = systemCall_waitIO(te);
	if(te2 != te){
		return 0;
	}
	return 1;
}

uint64_t systemCall_getTime(void){
	uintptr_t v[5];
	v[0] = systemCall6Return(SYSCALL_GET_TIME, v + 1, v + 2, v + 3, v + 4, v + 5);
	return COMBINE64(v[1], v[0]);
}
