#include"systemcall.h"
#include<std.h>
#include<common.h>
#include"handler.h"
#include"multiprocessor/spinlock.h"
#include"memory/memory.h"

struct SystemCallTable{
	Spinlock lock;
	unsigned int usedCount;
	struct SystemCallEntry{
		char name[MAX_NAME_LENGTH];
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
		printk("unregistered system call: %d\n",p->regs.eax);
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
	SystemCallFunction func = (SystemCallFunction)SYSTEM_CALL_ARGUMENT_1(p); // TODO: isValidKernelAddress
	uintptr_t arg = SYSTEM_CALL_ARGUMENT_2(p);
	SYSTEM_CALL_RETURN_VALUE_0(p) = registerService(systemCallTable, name, func, arg);
}
*/

static_assert(MAX_NAME_LENGTH / 4 == 4);
static void queryServiceHandler(InterruptParam *p){
	SystemCallTable *systemCallTable = (SystemCallTable*)p->argument;
	uint32_t name[MAX_NAME_LENGTH / 4 + 1];
	name[0] = SYSTEM_CALL_ARGUMENT_0(p);
	name[1] = SYSTEM_CALL_ARGUMENT_1(p);
	name[2] = SYSTEM_CALL_ARGUMENT_2(p);
	name[3] = SYSTEM_CALL_ARGUMENT_3(p);
	name[4] = '\0';
	SYSTEM_CALL_RETURN_VALUE_0(p) = queryService(systemCallTable, (const char*)name);
}

enum ServiceNameError systemCall_queryService(const char *name){
	if(strlen(name) > MAX_NAME_LENGTH){
		return INVALID_NAME;
	}
	uint32_t name4[MAX_NAME_LENGTH / 4];
	MEMSET0(name4);
	strncpy((char*)name4, name, MAX_NAME_LENGTH);
	return (enum ServiceNameError)systemCall4(SYSCALL_QUERY_SERVICE ,name4[0], name4[1], name4[2], name4[4]);
}

SystemCallTable *initSystemCall(InterruptTable *t){
	SystemCallTable *NEW(systemCallTable);

	assert(systemCallTable != NULL);
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
