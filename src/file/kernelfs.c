#include"file.h"
#include"interrupt/handler.h"
#include"interrupt/systemcall.h"
#include"memory/memory.h"
#include"task/task.h"
#include"multiprocessor/processorlocal.h"
#include"io/io.h"

typedef struct OpenFileRequest OpenFileRequest;
struct OpenFileRequest{
	IORequest ior;
	OpenFileRequest *next, **prev;
};

static void cancelOpenFileRequest(IORequest *ior){
	DELETE(ior->ioRequest);
	printk("deleted");
}

static int finishOpenFileRequest(IORequest *ior, uintptr_t *returnValues){
	returnValues[0] = (uintptr_t)ior->ioRequest;
	putPendingIO(ior);
	return 1;
}

static OpenFileRequest *createOpenFileRequest(void){
	OpenFileRequest *NEW(ofr);
	if(ofr == NULL)
		return NULL;
	initIORequest(&ofr->ior, ofr, cancelOpenFileRequest, finishOpenFileRequest);
	ofr->next = NULL;
	ofr->prev = NULL;
	return ofr;
}

static uintptr_t testOpenKFS(const char *fileName, uintptr_t length){
	//TODO: check fileName address
	if((uintptr_t)strlen("null") != length || strncmp(fileName, "null", length) != 0){
		return IO_REQUEST_FAILURE;
	}
	OpenFileRequest *ofr = createOpenFileRequest();
	putPendingIO(&ofr->ior);
	ofr->ior.handle(&ofr->ior);
	return (uintptr_t)ofr;
}

static void kernelFileServiceHandler(InterruptParam *p){
	sti();
	uintptr_t fileRequest = dispatchFileSystemCall(p,
		testOpenKFS,
		NULL,//readKFS,
		NULL,//writeKFS,
		NULL,//seekKFS,
		NULL//closeKFS
	);
	SYSTEM_CALL_RETURN_VALUE_0(p) = fileRequest;
}

#define KERNEL_FILE_SERVICE_NAME ("kernelfs")

void kernelFileService(void){
	int kfs = registerService(global.syscallTable,
		KERNEL_FILE_SERVICE_NAME, kernelFileServiceHandler, 0);
	if(kfs <= 0){
		systemCall_terminate();
	}
	int ok = addFileSystem(kfs, KERNEL_FILE_SERVICE_NAME, strlen(KERNEL_FILE_SERVICE_NAME));
	if(!ok){
		systemCall_terminate();
	}
	printk("kfs ok\n");
	while(1){
		sleep(1000);
	}
}

#ifndef NDEBUG
void testKFS(void);
void testKFS(void){
	uintptr_t r = systemCall_discoverFileSystem(KERNEL_FILE_SERVICE_NAME, strlen(KERNEL_FILE_SERVICE_NAME));
	assert(r != IO_REQUEST_FAILURE);
	int kfs;
	uintptr_t r2 = systemCall_waitIOReturn(r, 1, &kfs);
	assert(r == r2);
	r = systemCall_openFile(kfs, "abcdefg", strlen("abcdefg"));
	assert(r == IO_REQUEST_FAILURE);
	r = systemCall_openFile(kfs, "null", strlen("null"));
	assert(r != IO_REQUEST_FAILURE);
	//r2 = systemCall_waitIOReturn(r, 0);
	//assert(r2 == r);
	printk("testKFS ok\n");
	systemCall_terminate();
}
#endif
