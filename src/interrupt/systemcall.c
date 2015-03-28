#include"systemcall.h"
#include<std.h>
#include<common.h>
#include"handler.h"
#include"memory/memory.h"

struct SystemCallTable{
	SystemCallFunction call[NUMBER_OF_SYSTEM_CALLS];
};

void registerSystemCall(SystemCallTable *s, enum SystemCall systemCall, SystemCallFunction f){
	assert(s->call[systemCall] == NULL);
	s->call[systemCall] = f;
}

static SystemCallTable *systemCallTable = NULL;

static void systemCallHandler(InterruptParam *p){
	assert(p->regs.eax < NUMBER_OF_SYSTEM_CALLS);
	assert(systemCallTable->call[p->regs.eax] != NULL);
	(systemCallTable->call[p->regs.eax])(p);
	sti();
}

SystemCallTable *initSystemCall(InterruptTable *t){
	static int needInit = 1;
	if(needInit){
		needInit = 0;
		NEW(systemCallTable);
		int i;
		for(i = 0; i < NUMBER_OF_SYSTEM_CALLS; i++){
			systemCallTable->call[i] = NULL;
		}
	}
	registerInterrupt(t, SYSTEM_CALL, systemCallHandler, 0);
	return systemCallTable;
}
