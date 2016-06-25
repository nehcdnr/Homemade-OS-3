#ifndef SYSTEM_CALL_TABLE_H_INCLUDED
#define SYSTEM_CALL_TABLE_H_INCLUDED

#include "../../lib/systemcall.h"
#include"handler.h"

// see interruptdescriptor.c
#define SYSTEM_CALL_NUMBER(P) ((P)->regs.eax)
#define SYSTEM_CALL_ARGUMENT_0(P) ((P)->regs.edx)
#define SYSTEM_CALL_ARGUMENT_1(P) ((P)->regs.ecx)
#define SYSTEM_CALL_ARGUMENT_2(P) ((P)->regs.ebx)
#define SYSTEM_CALL_ARGUMENT_3(P) ((P)->regs.esi)
#define SYSTEM_CALL_ARGUMENT_4(P) ((P)->regs.edi)

#define SYSTEM_CALL_RETURN_VALUE_0(P) ((P)->regs.eax)
#define SYSTEM_CALL_RETURN_VALUE_1(P) ((P)->regs.edx)
#define SYSTEM_CALL_RETURN_VALUE_2(P) ((P)->regs.ecx)
#define SYSTEM_CALL_RETURN_VALUE_3(P) ((P)->regs.ebx)
#define SYSTEM_CALL_RETURN_VALUE_4(P) ((P)->regs.esi)
#define SYSTEM_CALL_RETURN_VALUE_5(P) ((P)->regs.edi)

//void copyArguments(uintptr_t *a, const InterruptParam *p, int argumentCount);
void copyReturnValues(InterruptParam *p, const uintptr_t *rv, int returnCount);

#define SYSTEM_CALL_MAX_ARGUMENT_COUNT (5)
#define SYSTEM_CALL_MAX_RETURN_COUNT (6)

typedef InterruptHandler SystemCallFunction;
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
	INVALID_NAME = -1024,
	SERVICE_EXISTING,
	SERVICE_NOT_EXISTING,
	TOO_MANY_SERVICES,
};

#define MAX_NAME_LENGTH (16)
typedef char ServiceName[MAX_NAME_LENGTH];

// return system call number
int registerService(
	SystemCallTable *systemCallTable,
	const char *name,
	SystemCallFunction func,
	uintptr_t arg
);
int systemCall_queryService(const char *name);
int systemCall_queryServiceN(const char *name, uintptr_t nameLength);

typedef struct InterruptTable InterruptTable;
SystemCallTable *initSystemCall(InterruptTable *t);

#define VIDEO_SERVICE_NAME ("video")

#endif
