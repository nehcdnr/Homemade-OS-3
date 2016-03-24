#include"common.h"
#include"std.h"
#include"io.h"
#include"keyboard.h"
#include"memory/memory.h"
#include"file/file.h"
#include"resource/resource.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"assembly/assembly.h"
#include"interrupt/systemcall.h"
#include"interrupt/handler.h"
#include"task/task.h"

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

int printString(const char *s, size_t length){
	int a;
	acquireLock(&kernelConsole.lock); //IMPROVE: 1 lock&unlock per kprintf
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
	cd->video = (uint16_t*)(KERNEL_LINEAR_BEGIN + defaultTextVideoAddress.value);
	/*mapKernelPages(
		defaultTextVideoAddress,
		DEFAULT_VIDEO_ADDRESS_END - DEFAULT_TEXT_VIDEO_ADDRESS,
		KERNEL_NON_CACHED_PAGE
	);*/
	cd->lock = initialSpinlock;
	updateVideoAddress(cd);
	updateCursor(cd);
	int c = 0;
	for(c = 0; c < cd->screenArea; c++){
		cd->video[c] = WHITE_SPACE;
	}
}

// command line

// return pointer to first non-space, set cmdLine to end of non-space
static const char *nextArgument(const char **cmdLine, uintptr_t *length){
	uintptr_t i = indexOfNot(*cmdLine, 0, *length, ' ');
	(*cmdLine) += i;
	(*length) -= i;
	if(*length == 0){
		return NULL;
	}
	i = indexOf(*cmdLine, 0, *length, ' ');
	const char *prevCmdLine = *cmdLine;
	(*cmdLine) += i;
	(*length) -= i;
	return prevCmdLine;
}

static uintptr_t nextHexadecimal(const char **cmdLine, uintptr_t *length){
	const char *arg;
	arg = nextArgument(cmdLine, length);
	if(arg == NULL){
		*cmdLine = NULL;
		return 0;
	}
	uintptr_t v;
	if(snscanf(arg,*cmdLine - arg, "%x", &v) != 1){
		*cmdLine = NULL;
		return 0;
	}
	return v;
}

static uintptr_t _openCommand(const char *cmdLine, uintptr_t length, OpenFileMode mode){
	const char *arg;
	arg = nextArgument(&cmdLine, &length);
	if(arg == NULL)
		return UINTPTR_NULL;
	printString(arg, cmdLine - arg);
	printk("\n");
	uintptr_t file = syncOpenFileN(arg, cmdLine - arg, mode);
	return file;
}

static uintptr_t openCommand(const char *cmdLine, uintptr_t length){
	return _openCommand(cmdLine, length, OPEN_FILE_MODE_0);
}

static uintptr_t enumCommand(const char *cmdLine, uintptr_t length){
	OpenFileMode mode = OPEN_FILE_MODE_0;
	mode.enumeration = 1;
	return _openCommand(cmdLine, length, mode);
}

static uintptr_t readCommand(const char *cmdLine, uintptr_t length){
	uintptr_t handle = nextHexadecimal(&cmdLine, &length);
	EXPECT(cmdLine != NULL);
	uint8_t *buffer = systemCall_allocateHeap(4096, USER_WRITABLE_PAGE);
	EXPECT(buffer != NULL);

	uintptr_t readSize = 4096, r, i;
	r = syncReadFile(handle, buffer, &readSize);
	if(r == IO_REQUEST_FAILURE || readSize == 0)
		break;

	for(i = 0; i < readSize; i++)
		printk("%c", buffer[i]);

	systemCall_releaseHeap(buffer);
	return readSize;
	// systemCall_releaseHeap(buffer);
	ON_ERROR;
	ON_ERROR;
	return UINTPTR_NULL;
}

static uintptr_t closeCommand(const char *cmdLine, uintptr_t length){
	uintptr_t handle = nextHexadecimal(&cmdLine, &length);
	if(cmdLine == NULL)
		return 0;
	return syncCloseFile(handle);
}

static uintptr_t dirCommand(const char *cmdLine, uintptr_t length){
	const char *arg = nextArgument(&cmdLine, &length);
	if(arg == NULL){
		printk("missing file name\n");
		return 0;
	}
	uintptr_t r, fileHandle = syncEnumerateFileN(arg, cmdLine - arg);
	if(fileHandle == IO_REQUEST_FAILURE){
		printk("open file error\n");
		return 0;
	}
	while(1){
		FileEnumeration fe;
		uintptr_t readSize = sizeof(fe);
		r = syncReadFile(fileHandle, &fe, &readSize);
		if(r == IO_REQUEST_FAILURE){
			printk("read file error\n");
			break;
		}
		if(readSize == 0)
			break;
		for(r = 0; r < fe.nameLength; r++){
			printk("%c", fe.name[r]);
		}
		printk("\n");
	}
	r = syncCloseFile(fileHandle);
	if(r == IO_REQUEST_FAILURE){
		printk("close file error\n");
	}
	return fileHandle;
}

static void parseCommand(const char *cmdLine, uintptr_t length){
	const struct{
		const char *string;
		uintptr_t (*handle)(const char*, uintptr_t);
	}cmdList[] = {
		{"open", openCommand},
		{"enum", enumCommand},
		{"read", readCommand},
		{"close", closeCommand},
		{"dir", dirCommand}
	};

	const char *arg = nextArgument(&cmdLine, &length);
	if(arg == NULL)
		return;
	uintptr_t argLen = (cmdLine - arg);

	unsigned i;
	for(i = 0; i < LENGTH_OF(cmdList); i++){
		if(
			argLen == (unsigned)strlen(cmdList[i].string) &&
			strncmp(arg, cmdList[i].string, argLen) == 0
		){
			int r = cmdList[i].handle(cmdLine, length);
			printk("\nreturn value = %d (0x%x)\n", r, r);
			return;
		}
	}
	printk("command not found: ");
	printString(arg, argLen);
	printk("\n");
}

static void printConsoleHandler(InterruptParam *p){
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

	parseCommand(cmdLine, index);
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

#define SHIFT_ARRAY_LENGTH (128)

static const char *createShiftKeyMap(void){
	char *NEW_ARRAY(shiftKeyMap, SHIFT_ARRAY_LENGTH);
	if(shiftKeyMap == NULL)
		return NULL;
	const char *s1 =
		"`1234567890-=qwertyuiop[]\\"
		"asdfghjkl;'"
		"zxcvbnm,./";
	const char *s2 =
		"~!@#$%^&*()_+QWERTYUIOP{}|"
		"ASDFGHJKL:\""
		"ZXCVBNM<>?";
	int a;
	for(a = 0;a < SHIFT_ARRAY_LENGTH; a++)
		shiftKeyMap[a] = a;
	for(a=0; s1[a]!='\0'; a++)
		shiftKeyMap[(int)s1[a]] = (int)s2[a];
	return shiftKeyMap;
}

static int shiftKey(const char *shiftKeyMap, int key){
	return (key < SHIFT_ARRAY_LENGTH && key >= 0? shiftKeyMap[key]: key);
}

struct KeyboardState{
	int shiftPressed;
};

static void kernelConsoleLoop(void){
	const char *shiftKeyMap = createShiftKeyMap();
	if(shiftKeyMap == NULL){
		printk("cannot allocate memory for kernel console\n");
		systemCall_terminate();
	}
	waitForFirstResource("ps2", RESOURCE_FILE_SYSTEM, matchName);
	uintptr_t kb = syncOpenFile("ps2:keyboard");
	if(kb == IO_REQUEST_FAILURE){
		printk("cannot open PS/2 keyboard\n");
		systemCall_terminate();
	}
	struct KeyboardState kbState = {0};
	char cmdLine[MAX_COMMAND_LINE_LENGTH];
	int index = 0;
	while(1){
		KeyboardEvent ke;
		uintptr_t readSize = sizeof(ke);
		uintptr_t r = syncReadFile(kb, &ke, &readSize);
		if(r == IO_REQUEST_FAILURE || readSize != sizeof(ke))
			break;
		// command line
		if(ke.isRelease != 0){
			switch(ke.key){
			case LEFT_SHIFT:
			case RIGHT_SHIFT:
				kbState.shiftPressed = 0;
				break;
			}
		}
		else{
			switch(ke.key){
			case LEFT_SHIFT:
			case RIGHT_SHIFT:
				kbState.shiftPressed = 1;
				break;
			case BACKSPACE:
				index = backspaceHandler(index);
				break;
			case ENTER:
				index = enterHandler(cmdLine, index);
				break;
			default:
				index = printHandler(cmdLine, index, (kbState.shiftPressed? shiftKey(shiftKeyMap, ke.key): ke.key));
				break;
			}
		}
	}
	printk("cannot read ps/2 keyboard");
	systemCall_terminate();
}

void kernelConsoleService(void){
	int result = registerService(global.syscallTable,
		KERNEL_CONSOLE_SERVICE_NAME, printConsoleHandler, (uintptr_t)&kernelConsole);
	EXPECT(result >= 0);
	kernelConsoleLoop();
	ON_ERROR;
	panic("cannot create kernelConsoleService");
}
