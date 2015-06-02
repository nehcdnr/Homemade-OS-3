#ifndef SYSTEMCALL_H_INCLUDED
#define SYSTEMCALL_H_INCLUDED

#include"handler.h"
enum SystemCall{
	// reserved
	// SYSCALL_TEST = 0
	SYSCALL_TASK_DEFINED = 1,
	SYSCALL_ACQUIRE_SEMAPHORE = 2,
	SYSCALL_RELEASE_SEMAPHORE = 3,
	SYSCALL_REGISTER_SERVICE = 4,
	SYSCALL_QUERY_SERVICE = 5,
	SYSCALL_ALLOCATE_HEAP = 6,
	SYSCALL_RELEASE_HEAP = 7,
	// runtime registration
	NUMBER_OF_RESERVED_SYSTEM_CALLS = 16,
	NUMBER_OF_SYSTEM_CALLS = 32
};
#define SYSCALL_SERVICE_BEGIN (NUMBER_OF_RESERVED_SYSTEM_CALLS)
#define SYSCALL_SERVICE_END (NUMBER_OF_SYSTEM_CALLS)

typedef InterruptHandler SystemCallFunction;

// see interruptdescriptor.c
void systemCall0(/*enum SystemCall*/int systemCallNumber);
void systemCall1(int systemCallNumber, uintptr_t param0);
#define SYSTEM_CALL_ARGUMENT_0(P) ((P)->regs.edx)
#define SYSTEM_CALL_ARGUMENT_1(P) ((P)->regs.ecx)
#define SYSTEM_CALL_ARGUMENT_2(P) ((P)->regs.ebx)

#define SYSTEM_CALL_RETURN_VALUE_0(P) ((P)->regs.eax)
#define SYSTEM_CALL_RETURN_VALUE_1(P) ((P)->regs.edx)

typedef struct SystemCallTable SystemCallTable;
// reserved system call
void registerSystemCall(
	SystemCallTable *s,
	enum SystemCall systemCall,
	SystemCallFunction func,
	uintptr_t arg
);
// runtime registration system call
enum ServiceNameError{
	SUCCESS,
	INVALID_NAME,
	SERVICE_EXISTING,
	SERVICE_NOT_EXISTING,
	TOO_MANY_SERVICE,
};
#define MAX_NAME_LENGTH (16)
enum ServiceNameError registerSystemService(
	SystemCallTable *systemCallTable,
	const char *name,
	SystemCallFunction func,
	uintptr_t arg
);
enum ServiceNameError querySystemService(
	SystemCallTable *systemCallTable,
	const char *name,
	unsigned int *syscallNumber
);

typedef struct InterruptTable InterruptTable;
SystemCallTable *initSystemCall(InterruptTable *t);

#define KEYBOARD_SERVICE_NAME ("keyboard")
#define MOUSE_SERVICE_NAME ("mouse")
#define VIDEO_SERVICE_NAME ("video")

#endif
