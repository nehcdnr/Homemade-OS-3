#include"file/file.h"

typedef enum ResourceType{
	RESOURCE_UNKNOWN = 0,
	RESOURCE_DISK_PARTITION,
	RESOURCE_FILE_SYSTEM,
	RESOURCE_DATA_LINK_DEVICE,
	MAX_RESOURCE_TYPE
}ResourceType;

typedef int (*MatchFunction)(const FileEnumeration*, uintptr_t);

int matchName(const FileEnumeration *fe, uintptr_t arg);
int matchWildcardName(const FileEnumeration *fe, uintptr_t arg);

uintptr_t enumNextResource(
	uintptr_t f, FileEnumeration *fe,
	uintptr_t arg, MatchFunction match
);
int waitForFirstResource(const char *name, ResourceType t, MatchFunction match);

const char *resourceTypeToFileName(ResourceType rt);

void initWaitableResource(void);
void deleteResource(ResourceType rt, const FileEnumeration *fe);
int createAddResource(ResourceType rt, const FileEnumeration *fe);
