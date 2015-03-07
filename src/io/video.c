#include"assembly/assembly.h"
#include"memory/memory.h"
#include"io.h"
#include"multiprocessor/spinlock.h"
#include"common.h"

struct ConsoleDisplay{
	int cursor;
	int maxRow, maxColumn;
	volatile uint16_t *video;
	Spinlock *lock;
};

#define DEFAULT_VEDIO_ADDRESS ((volatile uint16_t*)0xb8000)
ConsoleDisplay kernelConsole = {
	0,
	25, 80,
	DEFAULT_VEDIO_ADDRESS,
	nullSpinlock
};

static void updateCursor(ConsoleDisplay *cd){
    out(0x3d4, 0x0f);
	out(0x3d5, (cd->cursor >> 0) & 0xff);
    out(0x3d4, 0x0e);
	out(0x3d5, (cd->cursor >> 8) & 0xff);
}

int printString(const char *s);
int printString(const char *s){
	int a;
	//acquireLock(kernelConsole.lock); //TODO: 1 lock&unlock per kprintf
	a = printConsole(&kernelConsole, s);
	//releaseLock(kernelConsole.lock);
	return a;
}

int printConsole(ConsoleDisplay *cd, const char *s){
	int a;
	for(a = 0; s[a] != '\0'; a++){
		if(s[a]=='\n'){
			cd->cursor = cd->cursor - (cd->cursor % cd->maxColumn) + cd->maxColumn;
		}
		else{
			cd->video[cd->cursor] = s[a] + 0x0f00;
			cd->cursor++;
		}
		if(cd->cursor == cd->maxRow * cd->maxColumn){
			int b;
			for(b = 0; b < (cd->maxRow - 1) * cd->maxColumn; b++)
				cd->video[b] = cd->video[b + cd->maxColumn];
			for(b = (cd->maxRow - 1) * cd->maxColumn; b < cd->maxRow * cd->maxColumn; b++)
				cd->video[b] = ' ' + 0x0f00;
			cd->cursor -= cd->maxColumn;
		}
	}
	// updateCursor(cd); TODO: critical section
	return a;
}

ConsoleDisplay *initKernelConsole(MemoryManager *m){
	ConsoleDisplay *cd = &kernelConsole;
	cd->cursor = 0;
	cd->maxRow = 25;
	cd->maxColumn = 80;
	cd->video = DEFAULT_VEDIO_ADDRESS;
	cd->lock = createSpinlock(m);
	updateCursor(cd);
	int c = 0;
	for(c = 0; c < cd->maxColumn * cd->maxRow; c++){
		cd->video[c] = 0;
	}
	return cd;
}
