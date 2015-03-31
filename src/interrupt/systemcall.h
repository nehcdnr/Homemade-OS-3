#ifndef SYSTEMCALL_H_INCLUDED
#define SYSTEMCALL_H_INCLUDED

#include"handler.h"
enum SystemCall{
	// reserved
	SYSCALL_SUSPEND = 0,
	SYSCALL_TASK_DEFINED = 1,
	// runtime registration
	NUMBER_OF_RESERVED_SYSTEM_CALLS = 16,
	NUMBER_OF_SYSTEM_CALLS = 32
};
#define SYSCALL_SERVICE_BEGIN (NUMBER_OF_RESERVED_SYSTEM_CALLS)
#define SYSCALL_SERVICE_END (NUMBER_OF_SYSTEM_CALLS)

typedef void (*SystemCallFunction)(InterruptParam *p);

// see interruptdescriptor.c
void systemCall(/*enum SystemCall*/int eax);

typedef struct SystemCallTable SystemCallTable;
// reserved system call
void registerSystemCall(SystemCallTable *s, enum SystemCall systemCall, SystemCallFunction f);
// runtime registration system call
enum ServiceNameError{
	SUCCESS,
	INVALID_NAME,
	SERVICE_EXISTING,
	SERVICE_NOT_EXISTING,
	TOO_MANY_SERVICE,
};
#define MAX_NAME_LENGTH (16)
enum ServiceNameError registerSystemService(SystemCallTable *systemCallTable, const char *name, SystemCallFunction f);
enum ServiceNameError querySystemService(SystemCallTable *systemCallTable, const char *name, unsigned int *syscallNumber);

typedef struct InterruptTable InterruptTable;
SystemCallTable *initSystemCall(InterruptTable *t);

#define KEYBOARD_SERVICE_NAME ("keyboard")
#define MOUSE_SERVICE_NAME ("mouse")

#endif
