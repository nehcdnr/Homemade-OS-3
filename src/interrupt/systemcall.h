#ifndef SYSTEMCALL_H_INCLUDED
#define SYSTEMCALL_H_INCLUDED

#include"handler.h"
enum SystemCall{
	SYSCALL_SUSPEND = 0,
	SYSCALL_TASK_DEFINED = 1,
	NUMBER_OF_SYSTEM_CALLS
};

typedef void (*SystemCallFunction)(InterruptParam *p);

void systemCall(enum SystemCall eax);

typedef struct SystemCallTable SystemCallTable;

void registerSystemCall(SystemCallTable *s, enum SystemCall systemCall, SystemCallFunction f);

typedef struct InterruptTable InterruptTable;
SystemCallTable *initSystemCall(InterruptTable *t);

#endif
