#ifndef SYSTEM_CALL_H_INCLUDED
#define SYSTEM_CALL_H_INCLUDED

#include"std.h"

#define SYSTEM_CALL_VECTOR 126

enum SystemCall{
	// reserved
	// SYSCALL_TEST = 0
	SYSCALL_TASK_DEFINED = 1,
	SYSCALL_ACQUIRE_SEMAPHORE = 2,
	SYSCALL_RELEASE_SEMAPHORE = 3,
	//SYSCALL_REGISTER_SERVICE = 4,
	SYSCALL_QUERY_SERVICE = 5,
	SYSCALL_WAIT_IO = 6,
	SYSCALL_CANCEL_IO = 7,
	SYSCALL_ALLOCATE_HEAP = 8,
	SYSCALL_RELEASE_HEAP = 9,
	SYSCALL_TRANSLATE_PAGE = 10,
	//SYSCALL_REGISTER_RESOURCE = 11,
	SYSCALL_DISCOVER_RESOURCE = 12,
	//SYSCALL_CREATE_USER_SPACE = 13, CREATE_PROCESS
	SYSCALL_CREATE_USER_THREAD = 14,
	SYSCALL_TERMINATE = 15,
	SYSCALL_SET_ALARM = 16,
	// file
	SYSCALL_OPEN_FILE = 20,
	SYSCALL_CLOSE_FILE = 24,
	SYSCALL_READ_FILE = 25,
	SYSCALL_WRITE_FILE = 26,
	SYSCALL_SEEK_READ_FILE = 28,
	SYSCALL_SEEK_WRITE_FILE = 29,
	SYSCALL_GET_FILE_PARAMETER = 30,
	SYSCALL_SET_FILE_PARAMETER = 31,
	// runtime registration
	NUMBER_OF_RESERVED_SYSTEM_CALLS = 32,
	NUMBER_OF_SYSTEM_CALLS = 64
};
#define SYSCALL_SERVICE_BEGIN ((int)NUMBER_OF_RESERVED_SYSTEM_CALLS)
#define SYSCALL_SERVICE_END ((int)NUMBER_OF_SYSTEM_CALLS)

uintptr_t systemCall1(/*enum SystemCall*/int systemCallNumber);
uintptr_t systemCall2(int systemCallNumber, uintptr_t arg1);
uintptr_t systemCall3(int systemCallNumber, uintptr_t arg1, uintptr_t arg2);
uintptr_t systemCall4(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3);
uintptr_t systemCall5(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
	uintptr_t arg4);
uintptr_t systemCall6(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
	uintptr_t arg4, uintptr_t arg5);
uintptr_t systemCall6Return(int systemCallNumber, uintptr_t *arg1, uintptr_t *arg2, uintptr_t *arg3,
	uintptr_t *arg4, uintptr_t *arg5);

#endif
