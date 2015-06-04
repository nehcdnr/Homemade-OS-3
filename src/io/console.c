#include"common.h"
#include"std.h"
#include"io.h"
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"assembly/assembly.h"
#include"interrupt/systemcall.h"
#include"interrupt/handler.h"

typedef struct ConsoleDisplay{
	int cursor;
	int maxRow, maxColumn;
	volatile uint16_t *video;
	Spinlock lock;
}ConsoleDisplay;

#define DEFAULT_TEXT_VIDEO_ADDRESS ((uintptr_t)0xb8000)
#define VIDEO_ADDRESS_END ((uintptr_t)0xc0000)
ConsoleDisplay kernelConsole = {
	0,
	25, 80,
	(volatile uint16_t*)DEFAULT_TEXT_VIDEO_ADDRESS,
	NULL_SPINLOCK
};

static void updateVideoAddress(ConsoleDisplay *cd){
	uint16_t a;
	if(cd->cursor < cd->maxRow * cd->maxColumn){
		a = 0;
	}
	else{
		a = cd->maxColumn *	(cd->cursor / cd->maxColumn + 1 - cd->maxRow);
	}
	out8(0x3d4, 0x0c);
	out8(0x3d5, (a >> 8) & 0xff);
	out8(0x3d4, 0x0d);
	out8(0x3d5, a & 0xff);
}


static void updateCursor(ConsoleDisplay *cd){
    out8(0x3d4, 0x0f);
	out8(0x3d5, (cd->cursor >> 0) & 0xff);
    out8(0x3d4, 0x0e);
	out8(0x3d5, (cd->cursor >> 8) & 0xff);
}

/*
bit 7: blink
fore:
bit 8: blue
bit 9: green
bit 10: red
bit 11: light
back:
bit 12: blue
bit 13: green
bit 14: red
bit 15: light
*/
#define WHITE_SPACE (0x0f00 + ' ')
static void printChar(ConsoleDisplay *cd, uint8_t c, uint16_t backColor, uint16_t foreColor){
	int rc = cd->maxRow * cd->maxColumn;
	cd->video[cd->cursor] = c + (backColor << 12) + (foreColor << 8);
	if(cd->cursor >= rc){
		cd->video[cd->cursor - rc] = cd->video[cd->cursor];
	}
}

static int printConsole(ConsoleDisplay *cd, const char *s, size_t length){
	const int rc = cd->maxColumn * cd->maxRow;
	unsigned int a;
	for(a = 0; a < length; a++){
		if(s[a]=='\n'){
			int nextLine = cd->cursor - (cd->cursor % cd->maxColumn) + cd->maxColumn;
			while(cd->cursor != nextLine){
				printChar(cd, ' ', 0x0, 0x0);
				cd->cursor++;
			}
		}
		else{
			printChar(cd, s[a], 0x0, 0xf);
			cd->cursor++;
		}
		if(cd->cursor == rc * 2 - cd->maxColumn){
			cd->cursor -= rc;
		}
		if(cd->cursor % cd->maxColumn == 0){
			int c;
			for(c = cd->cursor; c < cd->cursor + cd->maxColumn; c++){
				cd->video[c] = WHITE_SPACE;
			}
		}
	}
	updateCursor(cd);
	updateVideoAddress(cd);
	return a;
}

int printString(const char *s, size_t length);
int printString(const char *s, size_t length){
	int a;
	acquireLock(&kernelConsole.lock); //TODO: 1 lock&unlock per kprintf
	a = printConsole(&kernelConsole, s, length);
	releaseLock(&kernelConsole.lock);
	return a;
}

void initKernelConsole(void){
	ConsoleDisplay *cd = &kernelConsole;
	PhysicalAddress defaultTextVideoAddress = {DEFAULT_TEXT_VIDEO_ADDRESS};
	cd->cursor = 0;
	cd->maxRow = 25;
	cd->maxColumn = 80;
	cd->video = mapKernelPage(defaultTextVideoAddress, VIDEO_ADDRESS_END - DEFAULT_TEXT_VIDEO_ADDRESS);
	cd->lock = initialSpinlock;
	updateVideoAddress(cd);
	updateCursor(cd);
	int c = 0;
	for(c = 0; c < cd->maxColumn * cd->maxRow; c++){
		cd->video[c] = WHITE_SPACE;
	}
}

static void syscall_printConsole(InterruptParam *p){
	//ConsoleDisplay *cd = (ConsoleDisplay*)p->argument;
	const char *string = (const char*)SYSTEM_CALL_ARGUMENT_0(p);
	size_t length = SYSTEM_CALL_ARGUMENT_1(p);
	//TODO: isValidUserAddress(string, length);
	printString(string, length);
	sti();
}

static void kernelConsoleLoop(int kbService){
	while(1){
		uintptr_t key = systemCall0(kbService);
		char s[4];
		s[0] = key;
		s[1]= '\0';
		printString(s, 1);
	}
}

void kernelConsoleService(void){
	int result;
	int kbService;
	kbService = result = systemCall1(SYSCALL_QUERY_SERVICE, (uintptr_t)KEYBOARD_SERVICE_NAME);
	EXPECT(result >= 0);
	result = //systemCall3(SYSCALL_REGISTER_SERVICE,
	registerSystemService(global.syscallTable,
	KERNEL_CONSOLE_SERVICE_NAME, syscall_printConsole, (uintptr_t)&kernelConsole);
	EXPECT(result >= 0);
	kernelConsoleLoop(kbService);
	ON_ERROR;
	ON_ERROR;
	panic("cannot create kernelConsoleService");
}
