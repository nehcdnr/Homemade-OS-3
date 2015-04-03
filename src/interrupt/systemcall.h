#ifndef SYSTEMCALL_H_INCLUDED
#define SYSTEMCALL_H_INCLUDED

#include"handler.h"
enum SystemCall{
	// reserved
	SYSCALL_SUSPEND = 0,
	SYSCALL_TASK_DEFINED = 1,
	SYSCALL_ACQUIRE_SEMAPHORE = 2,
	SYSCALL_RELEASE_SEMAPHORE = 3,
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
#define SYSTEM_CALL_ARGUMENT(N, P) (\
(N) == 0? (P)->regs.ebx: (\
(N) == 1? (P)->regs.ecx: (\
(N) == 2? (P)->regs.edx: (\
(N) == 3? (P)->regs.esi: (\
          (P)->regs.edi   \
)))))
#define SYSTEM_CALL_RETUEN_VALUE(P) ((P)->regs.eax)

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

#endif
