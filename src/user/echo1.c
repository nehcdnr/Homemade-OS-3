#include"systemcall.h"
#include"file.h"
#include"io.h"
#include"common.h"

#define CRASH __asm__("hlt\n")

int main(int argc, char *argv[]){
	uintptr_t f = syncOpenFileN("console:", strlen("console:"), OPEN_FILE_MODE_0);
	if(f == IO_REQUEST_FAILURE){
		CRASH;
	}
	const uintptr_t bufSize = 30;
	char *buf = systemCall_allocateHeap(bufSize, USER_WRITABLE_PAGE);
	if(buf == NULL){
		CRASH;
	}
	uintptr_t r;
	uintptr_t readSize = bufSize;
	r = syncReadFile(f, buf, &readSize);
	if(r == IO_REQUEST_FAILURE){
		CRASH;
	}
	//uintptr_t a;
	//for(a = 0; a < bufSize; a++){
	//	buf[a] = '0' + a % 10;
	//}
	uintptr_t writeSize = readSize;
	r = syncWriteFile(f, buf, &writeSize);
	if(r == IO_REQUEST_FAILURE || writeSize != readSize){
		CRASH;
	}
	int ok = systemCall_releaseHeap(buf);
	if(!ok){
		CRASH;
	}
	r = syncCloseFile(f);
	if(r == IO_REQUEST_FAILURE){
		CRASH;
	}
	systemCall_terminate();
	return 0;
}
