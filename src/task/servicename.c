#include"servicename.h"
#include"multiprocessor/spinlock.h"
#include"common.h"
#include"memory/memory.h"

#define MAX_NAME_LENGTH (16)
typedef struct ServiceName{
	char name[MAX_NAME_LENGTH];

	struct ServiceName *next;
}ServiceName;

static ServiceName *queryService(ServiceName *serviceList, const char *name){
	struct ServiceName *curr;
	for(curr = serviceList; curr != NULL; curr = curr->next){
		if(strncmp(name, curr->name, MAX_NAME_LENGTH) == 0){
			return curr;
		}
	}
	return NULL;
}

enum ServiceNameError registerService(const char *name){
	static ServiceName *serviceList = NULL;
	static Spinlock serviceListLock = INITIAL_SPINLOCK;

	int a;
	for(a = 0; a < MAX_NAME_LENGTH - 1 && name[a] != '\0'; a++);
	if(name[a] != '\0'){
		return NAME_TOO_LONG;
	}

	enum ServiceNameError r = SUCCESS;
	acquireLock(&serviceListLock);
	if(queryService(serviceList, name) != NULL){
		r = NAME_EXISTING;
	}
	else{
		ServiceName *NEW(sn);
		strncpy(sn->name, name, MAX_NAME_LENGTH);
		sn->next = serviceList;
		serviceList = sn;
	}
	releaseLock(&serviceListLock);
	return r;
}
