#include"common.h"
#include"std.h"
#include"io.h"
#include"keyboard.h"
#include"memory/memory.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"assembly/assembly.h"
#include"interrupt/systemcall.h"
#include"interrupt/handler.h"

typedef struct ConsoleDisplay{
	int screen;
	int cursor;
	int maxColumn, screenArea;
	volatile uint16_t *video;
	Spinlock lock;
}ConsoleDisplay;

#define DEFAULT_TEXT_VIDEO_ADDRESS ((uintptr_t)0xb8000)
#define DEFAULT_VIDEO_ADDRESS_END ((uintptr_t)0xc0000)

ConsoleDisplay kernelConsole = {
	0, 0,
	80, 25 * 80,
	(volatile uint16_t*)DEFAULT_TEXT_VIDEO_ADDRESS,
	NULL_SPINLOCK
};

static void updateVideoAddress(ConsoleDisplay *cd){
	out8(0x3d4, 0x0c);
	out8(0x3d5, (cd->screen >> 8) & 0xff);
	out8(0x3d4, 0x0d);
	out8(0x3d5, cd->screen & 0xff);
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
#define printWhiteSpace(CD) printChar((CD), ' ', 0x0, 0xf)
static void printChar(ConsoleDisplay *cd, uint8_t c, uint16_t backColor, uint16_t foreColor){
	cd->video[cd->cursor] = c + (backColor << 12) + (foreColor << 8);
	if(cd->cursor >= cd->screenArea){
		cd->video[cd->cursor - cd->screenArea] = cd->video[cd->cursor];
	}
}

static int cursorOnNewLine(ConsoleDisplay *cd){
	int rotated = 0;
	if(cd->cursor % cd->maxColumn == 0){
		if(cd->cursor == cd->screenArea * 2 - cd->maxColumn){
			cd->cursor -= cd->screenArea;
			rotated = 1;
		}
		int c;
		for(c = cd->cursor; c < cd->cursor + cd->maxColumn; c++){
			cd->video[c] = WHITE_SPACE;
		}
	}
	return rotated;
}

static void forwardScreen(ConsoleDisplay *cd, int rotated){
	if(rotated){
		cd->screen = 0;
	}
	while(cd->cursor < cd->screen || cd->cursor >= cd->screen + cd->screenArea){
		cd->screen += cd->maxColumn;
		if(cd->screen == cd->screenArea){
			cd->screen = 0;
		}
	}
}

static int printConsole(ConsoleDisplay *cd, const char *s, size_t length){
	unsigned int a;
	for(a = 0; a < length; a++){
		if(s[a]=='\n'){
			int nextLine = (cd->cursor + cd->maxColumn - (cd->cursor % cd->maxColumn));
			while(cd->cursor != nextLine){
				printWhiteSpace(cd);
				cd->cursor++;
			}
		}
		else{
			printChar(cd, s[a], 0x0, 0xf);
			cd->cursor++;
		}
		int rotated = cursorOnNewLine(cd);
		forwardScreen(cd, rotated);
	}
	return a;
}


static int printBackspace(int length){
	int a;
	acquireLock(&kernelConsole.lock);
	{
		ConsoleDisplay *cd = &kernelConsole;
		for(a = 0; a < length && cd->cursor > cd->screen; a++){
			cd->cursor--;
			printWhiteSpace(cd);
		}
		updateCursor(cd);
	}
	releaseLock(&kernelConsole.lock);
	return a;
}


int printString(const char *s, size_t length);
int printString(const char *s, size_t length){
	int a;
	acquireLock(&kernelConsole.lock); //TODO: 1 lock&unlock per kprintf
	a = printConsole(&kernelConsole, s, length);
	updateCursor(&kernelConsole);
	updateVideoAddress(&kernelConsole);
	releaseLock(&kernelConsole.lock);
	return a;
}

void initKernelConsole(void){
	ConsoleDisplay *cd = &kernelConsole;
	PhysicalAddress defaultTextVideoAddress = {DEFAULT_TEXT_VIDEO_ADDRESS};
	cd->screen = 0;
	cd->cursor = 0;
	cd->maxColumn = 80;
	cd->screenArea = cd->maxColumn * 25;
	cd->video = mapKernelPage(defaultTextVideoAddress, DEFAULT_VIDEO_ADDRESS_END - DEFAULT_TEXT_VIDEO_ADDRESS);
	cd->lock = initialSpinlock;
	updateVideoAddress(cd);
	updateCursor(cd);
	int c = 0;
	for(c = 0; c < cd->screenArea; c++){
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

#define MAX_COMMAND_LINE_LENGTH (160)
static int backspaceHandler(int index){
	if(index <= 0){
		return index;
	}
	index -= printBackspace(1);
	return index;
}

static int enterHandler(char *cmdLine, int index){
	cmdLine[index] = '\n';
	printString("\n", 1);
	return 0;
}

static int printHandler(char *cmdLine, int index, int key){
	if(index >= MAX_COMMAND_LINE_LENGTH - 1){
		return index;
	}
	if(key >= 256){
		key = '?';
	}
	cmdLine[index] = (key & 0xff);
	index += printString(cmdLine + index, 1);
	return index;
}

static void kernelConsoleLoop(int kbService){
	char cmdLine[MAX_COMMAND_LINE_LENGTH];
	int index = 0;
	while(1){
		uintptr_t key = systemCall0(kbService);
		// command line
		switch(key){
		case BACKSPACE:
			index = backspaceHandler(index);
			break;
		case ENTER:
			index = enterHandler(cmdLine, index);
			break;
		default:
			index = printHandler(cmdLine, index, key);
			break;
		}
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
