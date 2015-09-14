#include"multiprocessor/spinlock.h"

typedef enum ResourceType{
	RESOURCE_PCI_DEVICE = 0,
	RESOURCE_DISK_PARTITION,
	RESOURCE_FILE_SYSTEM,
	MAX_RESOURCE_TYPE
}ResourceType;

typedef struct Resource Resource;

typedef int (*MatchArguments)(Resource*, const uintptr_t*);
typedef int (*SetReturnValues)(Resource*, uintptr_t*);
struct Resource{
	void *instance;
	// return whether the listener is added
	MatchArguments matchArguments;
	// return number of return values
	SetReturnValues setReturnValues;
	Resource *next, **prev;
};

void initResource(
	Resource *this,
	void* instance,
	MatchArguments matchArguments,
	SetReturnValues setReturnValues
);
void addResource(ResourceType t, Resource* r);
void initResourceManager(SystemCallTable *t);
