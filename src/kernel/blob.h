#include<std.h>
#include<common.h>

// see src/blob_build/

typedef struct{
	const char *name;
	uintptr_t begin;
	uintptr_t end;
}BLOBAddress;

extern const BLOBAddress blobList[];

extern const int blobCount;
