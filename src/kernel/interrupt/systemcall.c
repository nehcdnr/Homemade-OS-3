#include"systemcall.h"
#include<std.h>
#include<common.h>
#include"handler.h"
#include"multiprocessor/spinlock.h"
#include"memory/memory.h"

#define I_REG1(ARG) "a"(systemCallNumber)
#define I_REG2(ARG) I_REG1(ARG), "d"(ARG##1)
#define I_REG3(ARG) I_REG2(ARG), "c"(ARG##2)
#define I_REG4(ARG) I_REG3(ARG), "b"(ARG##3)
#define I_REG5(ARG) I_REG4(ARG), "S"(ARG##4)
#define I_REG6(ARG) I_REG5(ARG), "D"(ARG##5)

#define O_REG1(ARG) "=a"(r)
#define O_REG2(ARG) O_REG1(ARG), "=d"(ARG##1)
#define O_REG3(ARG) O_REG2(ARG), "=c"(ARG##2)
#define O_REG4(ARG) O_REG3(ARG), "=b"(ARG##3)
#define O_REG5(ARG) O_REG4(ARG), "=S"(ARG##4)
#define O_REG6(ARG) O_REG5(ARG), "=D"(ARG##5)

#define SYSCALL_ASM "int $"SYSTEM_CALL_VECTOR_STRING"\n"
#define SYSCALL1_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG1(ARG)
#define SYSCALL2_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG2(ARG)
#define SYSCALL3_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG3(ARG)
#define SYSCALL4_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG4(ARG)
#define SYSCALL5_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG5(ARG)
#define SYSCALL6_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG6(ARG)
#define SYSCALL6RETURN_ASM(ARG) SYSCALL_ASM: O_REG6(ARG): I_REG6(ARG)

static_assert(sizeof(uint32_t) == sizeof(uintptr_t));

uintptr_t systemCall1(int systemCallNumber){
	uintptr_t r;
	__asm__(SYSCALL1_ASM(arg));
	return r;
}

uintptr_t systemCall2(int systemCallNumber, uintptr_t arg1){
	uintptr_t r;
	__asm__(SYSCALL2_ASM(arg));
	return r;
}

uintptr_t systemCall3(int systemCallNumber, uintptr_t arg1, uintptr_t arg2){
	uintptr_t r;
	__asm__(SYSCALL3_ASM(arg));
	return r;
}

uintptr_t systemCall4(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3){
	uintptr_t r;
	__asm__(SYSCALL4_ASM(arg));
	return r;
}

uintptr_t systemCall5(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
	uintptr_t arg4){
	uintptr_t r;
	__asm__(SYSCALL5_ASM(arg));
	return r;
}

uintptr_t systemCall6(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
	uintptr_t arg4, uintptr_t arg5){
	uintptr_t r;
	__asm__(SYSCALL6_ASM(arg));
	return r;
}

uintptr_t systemCall6Return(int systemCallNumber, uintptr_t *arg1, uintptr_t *arg2, uintptr_t *arg3,
	uintptr_t *arg4, uintptr_t *arg5){
	uintptr_t r;
	__asm__(SYSCALL6RETURN_ASM(*arg));
	return r;
}

void copyReturnValues(InterruptParam *p, const uintptr_t *rv, int returnCount){
	switch(returnCount){
	case 6:
		SYSTEM_CALL_RETURN_VALUE_5(p) = rv[5];
	case 5:
		SYSTEM_CALL_RETURN_VALUE_4(p) = rv[4];
	case 4:
		SYSTEM_CALL_RETURN_VALUE_3(p) = rv[3];
	case 3:
		SYSTEM_CALL_RETURN_VALUE_2(p) = rv[2];
	case 2:
		SYSTEM_CALL_RETURN_VALUE_1(p) = rv[1];
	case 1:
		SYSTEM_CALL_RETURN_VALUE_0(p) = rv[0];
		break;
	case 0:
	default:
		assert(0);
	}
}

void copyArguments(uintptr_t *a, const InterruptParam *p, int argumentCount){
	switch(argumentCount){
	case 5:
		a[4] = SYSTEM_CALL_ARGUMENT_4(p);
	case 4:
		a[3] = SYSTEM_CALL_ARGUMENT_3(p);
	case 3:
		a[2] = SYSTEM_CALL_ARGUMENT_2(p);
	case 2:
		a[1] = SYSTEM_CALL_ARGUMENT_1(p);
	case 1:
		a[0] = SYSTEM_CALL_ARGUMENT_0(p);
	case 0:
		break;
	default:
		assert(0);
	}
}

struct SystemCallTable{
	Spinlock lock;
	unsigned int usedCount;
	struct SystemCallEntry{
		ServiceName name;
		int number;
		SystemCallFunction call;
		uintptr_t argument;
	}entry[NUMBER_OF_SYSTEM_CALLS];
};

void registerSystemCall(
	SystemCallTable *s,
	enum SystemCall systemCall,
	SystemCallFunction func,
	uintptr_t arg
){
	assert(s->entry[systemCall].call == NULL);
	assert(systemCall < NUMBER_OF_RESERVED_SYSTEM_CALLS && systemCall >= 0);
	acquireLock(&s->lock);
	s->entry[systemCall].name[0] = '\0';
	s->entry[systemCall].number = systemCall;
	s->entry[systemCall].call = func;
	s->entry[systemCall].argument = arg;
	releaseLock(&s->lock);
}

static int queryServiceIndex(struct SystemCallTable *s, const char *name){
	unsigned int i;
	for(i = SYSCALL_SERVICE_BEGIN; i < s->usedCount; i++){
		if(strncmp(name, s->entry[i].name, MAX_NAME_LENGTH) == 0){
			return s->entry[i].number;
		}
	}
	return NUMBER_OF_SYSTEM_CALLS;
}

static int isValidName(const char *name){
	int a;
	for(a = 0; a < MAX_NAME_LENGTH - 1 && name[a] != '\0'; a++);
	if(name[a] != '\0' || name[0] == '\0'){
		return 0;
	}
	return 1;
}

enum ServiceNameError registerService(
	SystemCallTable *systemCallTable,
	const char *name,
	SystemCallFunction func,
	uintptr_t arg
){
	if(isValidName(name) == 0){
		return INVALID_NAME;
	}
	int r;
	acquireLock(&systemCallTable->lock);
	if(queryServiceIndex(systemCallTable, name) < NUMBER_OF_SYSTEM_CALLS){
		r = SERVICE_EXISTING;
	}
	else{
		int u = systemCallTable->usedCount;
		if(u == NUMBER_OF_SYSTEM_CALLS){
			r = TOO_MANY_SERVICES;
		}
		else{
			systemCallTable->usedCount++;
			struct SystemCallEntry *e = systemCallTable->entry + u;
			strncpy(e->name, name, MAX_NAME_LENGTH);
			e->number = u;
			e->call = func;
			e->argument = arg;
			r = u;
		}
	}
	releaseLock(&systemCallTable->lock);
	printk("registerService(%s), return %d\n", name, r);
	return r;
}

static int queryService(SystemCallTable *systemCallTable, const char *name){
	int r;
	if(isValidName(name) == 0){
		return INVALID_NAME;
	}
	acquireLock(&systemCallTable->lock);
	r = queryServiceIndex(systemCallTable, name);
	releaseLock(&systemCallTable->lock);

	if(r < NUMBER_OF_SYSTEM_CALLS){
		return r;
	}
	return SERVICE_NOT_EXISTING;
}

static void systemCallHandler(InterruptParam *p){
	assert(p->regs.eax < NUMBER_OF_SYSTEM_CALLS);
	SystemCallTable *s = (SystemCallTable*)p->argument;
	struct SystemCallEntry *e = s->entry + p->regs.eax;
	if(e->call == NULL){
		printk("warning: unregistered system call: %d\n",p->regs.eax);
		defaultInterruptHandler(p);
	}
	else{
		uintptr_t oldArgument = p->argument;
		p->argument = e->argument;
		e->call(p);
		p->argument = oldArgument;
	}
	sti();
}
/*
static void registerServiceHandler(InterruptParam *p){
	SystemCallTable *systemCallTable = (SystemCallTable*)p->argument;
	const char *name = (const char*)SYSTEM_CALL_ARGUMENT_0(p);
	SystemCallFunction func = (SystemCallFunction)SYSTEM_CALL_ARGUMENT_1(p); // isValidKernelAddress()
	uintptr_t arg = SYSTEM_CALL_ARGUMENT_2(p);
	SYSTEM_CALL_RETURN_VALUE_0(p) = registerService(systemCallTable, name, func, arg);
}
*/

static_assert(MAX_NAME_LENGTH / sizeof(uintptr_t) == 4);
static void queryServiceHandler(InterruptParam *p){
	SystemCallTable *systemCallTable = (SystemCallTable*)p->argument;
	uint32_t name[MAX_NAME_LENGTH / sizeof(uintptr_t) + 1];
	name[0] = SYSTEM_CALL_ARGUMENT_0(p);
	name[1] = SYSTEM_CALL_ARGUMENT_1(p);
	name[2] = SYSTEM_CALL_ARGUMENT_2(p);
	name[3] = SYSTEM_CALL_ARGUMENT_3(p);
	name[4] = '\0';
	SYSTEM_CALL_RETURN_VALUE_0(p) = queryService(systemCallTable, (const char*)name);
}

enum ServiceNameError systemCall_queryService(const char *name){
	return systemCall_queryServiceN(name, strlen(name));
}

enum ServiceNameError systemCall_queryServiceN(const char *name, uintptr_t nameLength){
	if(nameLength > MAX_NAME_LENGTH){
		return INVALID_NAME;
	}
	uintptr_t name4[MAX_NAME_LENGTH / sizeof(uintptr_t)];
	MEMSET0((void*)name4);
	strncpy((char*)name4, name, MAX_NAME_LENGTH);
	enum ServiceNameError r = systemCall5(SYSCALL_QUERY_SERVICE ,name4[0], name4[1], name4[2], name4[3]);
	assert((r < SYSCALL_SERVICE_END && r >= SYSCALL_SERVICE_BEGIN) || r < 0);
	return r;
}

SystemCallTable *initSystemCall(InterruptTable *t){
	SystemCallTable *NEW(systemCallTable);
	if(systemCallTable == NULL){
		panic("cannot initialize system call table");
	}
	systemCallTable->lock = initialSpinlock;
	systemCallTable->usedCount = SYSCALL_SERVICE_BEGIN;
	unsigned int i;
	for(i = 0; i < NUMBER_OF_SYSTEM_CALLS; i++){
		systemCallTable->entry[i].name[0] = '\0';
		systemCallTable->entry[i].number = i;
		systemCallTable->entry[i].call = NULL;
	}
	//registerSystemCall(systemCallTable, SYSCALL_REGISTER_SERVICE, registerServiceHandler, (uintptr_t)systemCallTable);
	registerSystemCall(systemCallTable, SYSCALL_QUERY_SERVICE, queryServiceHandler, (uintptr_t)systemCallTable);
	registerInterrupt(t, SYSTEM_CALL, systemCallHandler, (uintptr_t)systemCallTable);

	return systemCallTable;
}
