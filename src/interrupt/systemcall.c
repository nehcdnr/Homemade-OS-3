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
		unsigned int number;
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

static int _querySystemService(struct SystemCallTable *s, const char *name){
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

enum ServiceNameError registerSystemService(
	SystemCallTable *systemCallTable,
	const char *name,
	SystemCallFunction func,
	uintptr_t arg
){
	if(isValidName(name) == 0){
		return INVALID_NAME;
	}
	enum ServiceNameError r = SUCCESS;
	acquireLock(&systemCallTable->lock);
	if(_querySystemService(systemCallTable, name) < NUMBER_OF_SYSTEM_CALLS){
		r = SERVICE_EXISTING;
	}
	else{
		unsigned int u = systemCallTable->usedCount;
		if(u == NUMBER_OF_SYSTEM_CALLS){
			r = TOO_MANY_SERVICE;
		}
		else{
			systemCallTable->usedCount++;
			struct SystemCallEntry *e = systemCallTable->entry + u;
			strncpy(e->name, name, MAX_NAME_LENGTH);
			e->number = u;
			e->call = func;
			e->argument = arg;
		}
	}
	releaseLock(&systemCallTable->lock);
	return r;
}

enum ServiceNameError querySystemService(SystemCallTable *systemCallTable, const char *name, unsigned int *syscallNumber){
	enum ServiceNameError r = SUCCESS;
	if(isValidName(name) == 0){
		return INVALID_NAME;
	}
	acquireLock(&systemCallTable->lock);
	*syscallNumber = _querySystemService(systemCallTable, name);
	releaseLock(&systemCallTable->lock);

	if(*syscallNumber >= NUMBER_OF_SYSTEM_CALLS){
		return SERVICE_NOT_EXISTING;
	}
	return r;
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

static void syscall_registerService(InterruptParam *p){
	SystemCallTable *systemCallTable = (SystemCallTable*)p->argument;
	const char *name = (const char*)SYSTEM_CALL_ARGUMENT_0(p);
	SystemCallFunction func = (SystemCallFunction)SYSTEM_CALL_ARGUMENT_1(p);
	uintptr_t arg = SYSTEM_CALL_ARGUMENT_2(p);
	SYSTEM_CALL_RETURN_VALUE_0(p) = registerSystemService(systemCallTable, name, func, arg);
}

static void syscall_queryService(InterruptParam *p){
	SystemCallTable *systemCallTable = (SystemCallTable*)p->argument;
	const char *name = (const char*)SYSTEM_CALL_ARGUMENT_0(p);
	unsigned int number = SYSTEM_CALL_ARGUMENT_1(p); //TODO: address check
	SYSTEM_CALL_RETURN_VALUE_0(p) = querySystemService(systemCallTable, name, &number);
	SYSTEM_CALL_RETURN_VALUE_1(p) = number;
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
	registerSystemCall(systemCallTable, SYSCALL_REGISTER_SERVICE, syscall_registerService, (uintptr_t)systemCallTable);
	registerSystemCall(systemCallTable, SYSCALL_QUERY_SERVICE, syscall_queryService, (uintptr_t)systemCallTable);
	registerInterrupt(t, SYSTEM_CALL, systemCallHandler, (uintptr_t)systemCallTable);

	return systemCallTable;
}
