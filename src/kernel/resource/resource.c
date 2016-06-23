#include"common.h"
#include"kernel.h"
#include"resource.h"
#include"task/task.h"
#include"multiprocessor/spinlock.h"
#include"task/exclusivelock.h"
#include"memory/memory.h"
#include"file/file.h"

typedef struct Resource{
	struct ResourceEnumerator *waiting;
	Spinlock *lock;
	struct Resource *next, **prev;

	FileEnumeration description;
}Resource;

typedef int IsFileEnumEqual(const FileEnumeration*, const FileEnumeration*);

typedef struct ResourceList{
	// uintptr_t typeFileNameLength
	const char *typeFileName;
	uintptr_t typeStringLength;
	const char *typeString;
	Spinlock lock;
	Resource *head;
	Resource tail;
	IsFileEnumEqual *isFileEnumEqual;
}ResourceList;


typedef struct ResourceEnumerator{
	Spinlock *lock; // protect currentWaiting, rwfrList, next, and prev
	Resource *currentWaiting;
	struct ReadEnumRequest *rwfrList;

	struct ResourceEnumerator *next, **prev;
}ResourceEnumerator;

typedef struct ReadEnumRequest{
	RWFileRequest *rwfr;
	FileEnumeration *fe;

	Spinlock *lock;
	struct ReadEnumRequest *next, **prev;
}ReadEnumRequest;

// see WaitingList->tail
static int isWaitingListTail_noLock(ResourceEnumerator *re){
	assert(isAcquirable(re->lock) == 0);
	return re->currentWaiting->next == NULL;
}

static ResourceEnumerator *createWaitingIterator(ResourceList *rl){
	ResourceEnumerator *NEW(re);
	EXPECT(re != NULL);
	re->lock = &rl->lock;
	acquireLock(re->lock);
	Resource *resource = rl->head;
	re->currentWaiting = resource;
	re->rwfrList = NULL;
	re->next = NULL;
	re->prev = NULL;
	ADD_TO_DQUEUE(re, &resource->waiting);
	releaseLock(re->lock);
	return re;
	// DELETE(re);
	ON_ERROR;
	return NULL;
}

static void deleteWaitingIterator(ResourceEnumerator *re){
	acquireLock(re->lock);
	assert(re->rwfrList == NULL);
	REMOVE_FROM_DQUEUE(re);
	releaseLock(re->lock);
	DELETE(re);
}

static ReadEnumRequest *iterateNext_noLock(ResourceEnumerator *re, int handleRequest){
	if(isWaitingListTail_noLock(re)){
		return NULL;
	}
	struct Resource *resource = re->currentWaiting;
	ReadEnumRequest *req;
	if(handleRequest == 0){
		req = NULL;
	}
	else{
		for(req = re->rwfrList; req != NULL; req = req->next){
			if(setRWFileIONotCancellable(req->rwfr)){
				(*req->fe) = resource->description;
				REMOVE_FROM_DQUEUE(req);
				break;
			}
		}
		if(req == NULL){
			return NULL;
		}
	}
	REMOVE_FROM_DQUEUE(re);
	resource = resource->next;
	re->currentWaiting = resource;
	assert(re->lock == resource->lock);
	ADD_TO_DQUEUE(re, &resource->waiting);
	// return NULL if handleRequest == 0
	return req;
}

static ResourceList resourceList[MAX_RESOURCE_TYPE];

static int isResourceNameEqual(const FileEnumeration *fe1, const FileEnumeration *fe2){
	return isStringEqual(fe1->name, fe1->nameLength, fe2->name, fe2->nameLength);
}

// return 1, 0, -1
static int isDiskPartitionEqual(const FileEnumeration *fe1, const FileEnumeration *fe2){
	if(isStringEqual(fe1->name, fe1->nameLength, fe2->name, fe2->nameLength) == 0)
		return 0;
	if(fe1->diskPartition.startLBA != fe2->diskPartition.startLBA)
		return 0;
	return 1;
}

static Resource *searchWaitable_noLock(ResourceList *rl, const FileEnumeration *fe){
	Resource *r;
	for(r = rl->head; r != &rl->tail; r = r->next){
		if(rl->isFileEnumEqual(&r->description, fe)){
			return r;
		}
	}
	return NULL;
}

static ReadEnumRequest *createEnumNextRequest(RWFileRequest *rwfr, FileEnumeration *fe, ResourceEnumerator *re){
	ReadEnumRequest *NEW(req);
	if(req == NULL){
		return NULL;
	}
	req->rwfr = rwfr;
	req->fe = fe;
	req->lock = re->lock;
	req->next = NULL;
	req->prev = NULL;
	return req;
}

static void cancelReadEnumWaitable(void *arg){
	ReadEnumRequest *req = arg;
	acquireLock(req->lock);
	REMOVE_FROM_DQUEUE(req);
	releaseLock(req->lock);
	DELETE(req);
}

static void completeEnumNextRequest(ReadEnumRequest *req){
	assert(IS_IN_DQUEUE(req) == 0);
	completeRWFileIO(req->rwfr, sizeof(*req->fe), 0);
	DELETE(req);
}

static void initWaitable(Resource *r, const FileEnumeration *fe){
	r->waiting = NULL;
	r->lock = NULL;
	r->next = NULL;
	r->prev = NULL;
	r->description = *fe;
}

void deleteResource(ResourceType rt, const FileEnumeration *fe){
	ResourceList *rl = resourceList + rt;
	acquireLock(&rl->lock);
	Resource *r = searchWaitable_noLock(rl, fe);
	assert(r != NULL);
	while(r->waiting != NULL){
		struct ResourceEnumerator *re = r->waiting;
		iterateNext_noLock(re, 0);
	}
	REMOVE_FROM_DQUEUE(r);
	releaseLock(&rl->lock);
	DELETE(r);
}

static int addWaitable(Resource *r, ResourceList *rl){
	r->lock = &rl->lock;
	acquireLock(r->lock);
	ReadEnumRequest *completed = NULL;
	int ok = (searchWaitable_noLock(rl, &r->description) == NULL);
	if(ok){
		Resource **p = rl->tail.prev;
		ADD_TO_DQUEUE(r, p);
		// 1. move all enumerator from tail to newly added resource
		assert(r->waiting == NULL);
		while(rl->tail.waiting != NULL){
			ResourceEnumerator *re = rl->tail.waiting;
			REMOVE_FROM_DQUEUE(re);
			re->currentWaiting = r;
			ADD_TO_DQUEUE(re, &r->waiting);
		}
		// 2. move enumerator from newly added resource to tail if it has pending and not cancelled requests
		ResourceEnumerator **rePrev = &r->waiting;
		while(1){
			ResourceEnumerator *re = (*rePrev);
			if(re == NULL)
				break;
			ReadEnumRequest *req = iterateNext_noLock(re, 1);
			if(req != NULL){
				// re is in rl->tail->waiting. do not change rePrev
				ADD_TO_DQUEUE(req, &completed);
				assert(re != (*rePrev));
			}
			else{
				// re is in r->waiting. go to next enumerator
				rePrev = &re->next;
			}
		}
	}
	releaseLock(r->lock);
	// 3. delete completed requests
	while(completed != NULL){
		ReadEnumRequest *req = completed;
		REMOVE_FROM_DQUEUE(req);
		assert(req != completed);
		completeEnumNextRequest(req);
	}
	return ok;
}

int createAddResource(ResourceType rt, const FileEnumeration *fe){
	Resource *NEW(r);
	EXPECT(r != NULL);
	initWaitable(r, fe);
	int ok = addWaitable(r, resourceList + rt);
	EXPECT(ok);
	return 1;

	ON_ERROR;
	ON_ERROR;
	return 0;
}

static int readEnumWaitable(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize){
	EXPECT(bufferSize >= sizeof(FileEnumeration));
	ResourceEnumerator *re = getFileInstance(of);
	FileEnumeration *fe = (FileEnumeration*)buffer;
	struct ReadEnumRequest *req = createEnumNextRequest(rwfr, fe, re);
	EXPECT(req != NULL);
	acquireLock(re->lock);
	ADD_TO_DQUEUE(req, &re->rwfrList);
	setRWFileIOCancellable(req->rwfr, req, cancelReadEnumWaitable);
	struct ReadEnumRequest *req2 = iterateNext_noLock(re, 1);
	assert(req2 == NULL || req2 == req);
	releaseLock(re->lock);
	// if we want to support cancel by other thread, must lock RWFileRequest
	if(req2 != NULL){
		completeEnumNextRequest(req);
	}
	return 1;
	ON_ERROR;
	ON_ERROR;
	return 0;
}

static void closeEnumWaitable(CloseFileRequest *cfr, OpenedFile *of){
	ResourceEnumerator *re = getFileInstance(of);
	deleteWaitingIterator(re);
	completeCloseFile(cfr);
}

static int enumWaitable(OpenFileRequest *ofr, const char *name, uintptr_t nameLength, OpenFileMode mode){
	EXPECT(mode.enumeration != 0);
	int a;
	for(a = 0 ; a < MAX_RESOURCE_TYPE; a++){
		if(isStringEqual(name, nameLength,
			resourceList[a].typeString, resourceList[a].typeStringLength)){
			break;
		}
	}
	EXPECT(a < MAX_RESOURCE_TYPE);
	ResourceEnumerator *re = createWaitingIterator(&resourceList[a]);
	EXPECT(re != NULL);
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.read = readEnumWaitable;
	ff.close = closeEnumWaitable;
	completeOpenFile(ofr, re, &ff);
	return 1;

	ON_ERROR;
	ON_ERROR;
	ON_ERROR;
	return 0;
}

int matchName(const FileEnumeration *fe, uintptr_t arg){
	const char *name = (const char*)arg;
	return isStringEqual(fe->name, fe->nameLength, name, strlen(name));
}

int matchWildcardName(const FileEnumeration *fe, uintptr_t arg){
	const char *name = (const char*)arg;
	return matchWildcardString(fe->name, fe->nameLength, name, strlen(name));
}

uintptr_t enumNextResource(
	uintptr_t f, FileEnumeration *fe,
	uintptr_t arg, MatchFunction match
){
	while(1){
		uintptr_t readSize = sizeof(*fe);
		uintptr_t r = syncReadFile(f, fe, &readSize);
		if(r == IO_REQUEST_FAILURE || readSize != sizeof(*fe))
			return 0;
		if(match(fe, arg))
			return readSize;
	}
}

int waitForFirstResource(const char *name, ResourceType t, MatchFunction match){
	uintptr_t waitFS = syncEnumerateFile(resourceTypeToFileName(t));
	EXPECT(waitFS != IO_REQUEST_FAILURE);
	FileEnumeration fe;
	uintptr_t r = enumNextResource(waitFS, &fe, (uintptr_t)name, match);
	EXPECT(r == sizeof(fe));
	r = syncCloseFile(waitFS);
	return r != IO_REQUEST_FAILURE;

	ON_ERROR;
	syncCloseFile(waitFS);
	ON_ERROR;
	return 0;
}

static void initWaitableTail(Resource *r, ResourceList *rl){
	FileEnumeration fe;
	MEMSET0(&fe);
	initWaitable(r, &fe);
	rl->tail.lock = &rl->lock;
	ADD_TO_DQUEUE(&rl->tail, &rl->head);
}

const char *resourceTypeToFileName(ResourceType rt){
	return resourceList[rt].typeFileName;
}

// func is a function checking whether there is a existing resource
static void initResourceList(ResourceType rt, const char *typeFileName, IsFileEnumEqual *func){
	ResourceList *rl = resourceList + rt;
	rl->typeFileName = typeFileName;
	int i;
	for(i = 0; typeFileName[i] != ':'; i++);
	rl->typeString = typeFileName + i + 1;
	rl->typeStringLength = strlen(rl->typeString);
	rl->lock = initialSpinlock;
	rl->head = NULL;
	initWaitableTail(&rl->tail, rl);
	rl->isFileEnumEqual = func;
}

static_assert(MAX_RESOURCE_TYPE == 4);

void initWaitableResource(void){
#define R "resource:"
	const uintptr_t prefixLength = strlen(R);
	initResourceList(RESOURCE_UNKNOWN, R "?", isResourceNameEqual);
	initResourceList(RESOURCE_DISK_PARTITION, R "diskpartition", isDiskPartitionEqual);
	initResourceList(RESOURCE_FILE_SYSTEM, R "filesystem", isResourceNameEqual);
	initResourceList(RESOURCE_DATA_LINK_DEVICE, R "datalink", isResourceNameEqual);

	FileNameFunctions ff = INITIAL_FILE_NAME_FUNCTIONS;
	ff.open = enumWaitable;
	if(addFileSystem(&ff, R, prefixLength - 1 /*remove colon*/) == 0){
		assert(0);
	}
#undef R
}

#ifndef NDEBUG
#include"multiprocessor/processorlocal.h"

static void _testResource(void){
	uintptr_t r;
	uintptr_t f = syncEnumerateFile("resource:?");
	assert(f != IO_REQUEST_FAILURE);
	FileEnumeration fe1;
	fe1.nameLength = snprintf(fe1.name, sizeof(fe1.name), "SERVICE:NAME%x", (int)processorLocalTask());
	int ok = createAddResource(RESOURCE_UNKNOWN, &fe1);
	assert(ok);
	FileEnumeration fe2;
	// test normal read
	uintptr_t readSize;
	while(1){
		MEMSET0(&fe2);
		readSize = sizeof(fe2);
		r = syncReadFile(f, &fe2, &readSize);
		assert(r != IO_REQUEST_FAILURE && readSize == sizeof(fe2));
		if(isStringEqual(fe2.name, fe2.nameLength, fe1.name, fe1.nameLength)){
			break;
		}
	}
	// test cancel
	readSize = sizeof(fe2);
	r = systemCall_readFile(f, &fe2, readSize);
	assert(r != IO_REQUEST_FAILURE);
	ok = systemCall_cancelIO(r);
	if(!ok){
		// IMPROVE: an system call to check whether io is completed
		uintptr_t r2 = systemCall_waitIOReturn(r, 1, &readSize);
		assert(r2 != IO_REQUEST_FAILURE && readSize == sizeof(fe2));
	}
	// finish
	deleteResource(RESOURCE_UNKNOWN, &fe1);
	r = syncCloseFile(f);
	assert(r != IO_REQUEST_FAILURE);
}

void testResource(void);
void testResource(void){
	uintptr_t x;
	for(x = 0; x < 100; x++){
		_testResource();
	}
	printk("testResource ok\n");
	systemCall_terminate();
}

#endif
