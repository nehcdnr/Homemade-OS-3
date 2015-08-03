
#include"interrupt/systemcall.h"
#include"multiprocessor/processorlocal.h"
#include"io/io.h"
#include"memory/memory.h"
#include"resource.h"

typedef struct NewResource NewResource;
struct NewResource{
	Resource *resource;
	NewResource **prev, *next;
};

typedef struct NewResourceEvent NewResourceEvent;
struct NewResourceEvent{
	IORequest this;
	Spinlock *managerLock;
	Spinlock lock;
	NewResource *newResourceList, *servingResource;
	uintptr_t arguments[SYSTEM_CALL_MAX_ARGUMENT_COUNT];
	NewResourceEvent **prev, *next;
};

// assume locked
static void serveNewResource(NewResourceEvent *nre){
	NewResource *nr = nre->newResourceList;
	if(nr == NULL){
		return;
	}
	if(nre->servingResource != NULL){
		return;
	}
	REMOVE_FROM_DQUEUE(nr);
	ADD_TO_DQUEUE(nr, &nre->servingResource);
	nre->this.handle(&nre->this);
}

static void cancelNewResourceEvent(IORequest *ior){
	NewResourceEvent *nre = ior->ioRequest;
	acquireLock(nre->managerLock);
	REMOVE_FROM_DQUEUE(nre); // resourceManager->listener
	releaseLock(nre->managerLock);
	acquireLock(&nre->lock);
	NewResource *cleanList[2] = {nre->newResourceList, nre->servingResource};
	nre->newResourceList = NULL;
	nre->servingResource = NULL;
	releaseLock(&nre->lock);
	unsigned i;
	for(i = 0; i < LENGTH_OF(cleanList); i++){
		while(cleanList[i] != NULL){
			NewResource *t = cleanList[i];
			cleanList[i] = cleanList[i]->next;
			DELETE(t);
		}
	}
	DELETE(nre);
}

static int finishNewResourceEvent(IORequest *ior, uintptr_t *returnValues){
	NewResourceEvent *nre = ior->ioRequest;
	acquireLock(&nre->lock);
	NewResource *nr = nre->servingResource;
	REMOVE_FROM_DQUEUE(nr);
	putPendingIO(&nre->this);
	serveNewResource(nre);
	releaseLock(&nre->lock);
	int returnCount = nr->resource->setReturnValues(nr->resource, returnValues);
	DELETE(nr);
	return returnCount;
}

static NewResourceEvent *createNewResourceEvent(Spinlock *lock, NewResourceEvent **list, const InterruptParam *p){
	NewResourceEvent *NEW(nre);
	if(nre == NULL){
		return NULL;
	}
	initIORequest(&nre->this, nre,
		cancelNewResourceEvent, finishNewResourceEvent);
	nre->managerLock = lock;
	nre->lock = initialSpinlock;
	nre->newResourceList = NULL;
	nre->servingResource = NULL;
	nre->next = NULL;
	nre->prev = NULL;
	copyArguments(nre->arguments, p, SYSTEM_CALL_MAX_ARGUMENT_COUNT);
	ADD_TO_DQUEUE(nre, list);
	return nre;
}

// Resource-NewResourceEvent relation is one-to-many.
typedef struct ResourceManager{
	Spinlock lock;
	Resource *list;
	NewResourceEvent *listener;
}ResourceManager;
static ResourceManager resourceManager[MAX_RESOURCE_TYPE];

// return 1 if added
static int addResourceToListener(Resource *r, NewResourceEvent *nre){
	if(r->matchArguments(r, nre->arguments) == 0)
		return 0;
	NewResource *NEW(nr);
	if(nr == NULL){
		printk("warning: cannot allocate NewResource");
		return 0;
	}
	nr->resource = r;
	nr->prev = NULL;
	nr->next = NULL;
	ADD_TO_DQUEUE(nr, &(nre->newResourceList));
	return 1;
}

static void discoverResourceHandler(InterruptParam *p){
	sti();
	ResourceType t = SYSTEM_CALL_ARGUMENT_0(p);
	Resource *r;
	ResourceManager *rm = resourceManager + t;
	NewResourceEvent *nre = createNewResourceEvent(&rm->lock, &rm->listener, p);
	EXPECT(nre != NULL);
	putPendingIO(&nre->this);
	acquireLock(&nre->lock);
	for(r = rm->list; r != NULL; r = r->next){
		addResourceToListener(r, nre);
	}
	serveNewResource(nre);
	releaseLock(&nre->lock);
	SYSTEM_CALL_RETURN_VALUE_0(p) = (uintptr_t)nre;
	return;
	ON_ERROR;
	SYSTEM_CALL_RETURN_VALUE_0(p) = UINTPTR_NULL;
}

void addResource(ResourceType t, Resource* r){
	ResourceManager *rm = resourceManager + t;
	NewResourceEvent *nre;
	acquireLock(&rm->lock);
	ADD_TO_DQUEUE(r, &rm->list);
	for(nre = rm->listener; nre != NULL; nre = nre->next){
		if(addResourceToListener(r, nre)){
			serveNewResource(nre);
		}
	}
	releaseLock(&rm->lock);
}

void initResource(
	Resource *this,
	void* instance,
	MatchArguments matchArguments,
	SetReturnValues setReturnValues
){
	this->instance = instance;
	this->matchArguments = matchArguments;
	this->setReturnValues = setReturnValues;
	this->next = NULL;
	this->prev = NULL;
}

void initResourceManager(SystemCallTable *t){
	unsigned i;
	for(i = 0; i < LENGTH_OF(resourceManager); i++){
		resourceManager[i].lock = initialSpinlock;
		resourceManager[i].list = NULL;
	}
	registerSystemCall(t, SYSCALL_DISCOVER_RESOURCE, discoverResourceHandler, 0);
}
