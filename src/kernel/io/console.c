#include"common.h"
#include"kernel.h"
#include"io.h"
#include"keyboard.h"
#include"memory/memory.h"
#include"file/fileservice.h"
#include"io/fifo.h"
#include"resource/resource.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"assembly/assembly.h"
#include"interrupt/handler.h"
#include"interrupt/systemcalltable.h"
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

ConsoleDisplay kernelDisplay = {
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

static int printBackspace(ConsoleDisplay *cd, int length){
	int a;
	acquireLock(&cd->lock);
	{
		for(a = 0; a < length && cd->cursor > cd->screen; a++){
			cd->cursor--;
			printWhiteSpace(cd);
		}
		updateCursor(cd);
	}
	releaseLock(&cd->lock);
	return a;
}

static int printkBackspace(int length){
	return printBackspace(&kernelDisplay, length);
}

static int printString(ConsoleDisplay *cd, const char *s, size_t length){
	int a;
	acquireLock(&cd->lock); //IMPROVE: 1 lock&unlock per kprintf
	a = printConsole(cd, s, length);
	updateCursor(cd);
	updateVideoAddress(cd);
	releaseLock(&cd->lock);
	return a;
}

int printkString(const char *s, size_t length){
	return printString(&kernelDisplay, s, length);
}

void initKernelConsole(void){
	ConsoleDisplay *cd = &kernelDisplay;
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
	printkString(arg, cmdLine - arg);
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

static uintptr_t runCommand(const char *cmdLine, uintptr_t length){
	const char *programName = nextArgument(&cmdLine, &length);
	if(programName == NULL){
		printk("missing executable file name\n");
		return 0;
	}
	//TODO: arguments
	uintptr_t programNameLength = cmdLine - programName;
	Task *t = createUserTaskFromELF(programName, programNameLength, 3);
	if(t == NULL){
		printk("failed to start task\n");
	}
	resume(t);
	return (uintptr_t)t;
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
		{"dir", dirCommand},
		{"run", runCommand}
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
	printkString(arg, argLen);
	printk("\n");
}

typedef struct RWConsoleRequest{
	RWFileRequest *rwfr;
	uint8_t *buffer;
	uintptr_t bufferSize;
	struct RWConsoleRequest *next, **prev;
}RWConsoleRequest;


#define MAX_HISTORY_LENGTH (8)
#define MAX_COMMAND_LINE_LENGTH (160)
typedef struct{
	char line[MAX_COMMAND_LINE_LENGTH];
	int lineEnd;
	Spinlock lock;
	int listenerCount;
	// if openCount == 0, then the console will execute the command
	// Otherwise, the command is sent to lineFIFO
	// see enterHandler()
	FIFO *lineFIFO;
	RWConsoleRequest *requestHead;
}ConsoleCommandLine;

// see kernelConsoleService
static ConsoleCommandLine kernelCommand;

static int initConsoleCommandLine(ConsoleCommandLine *cmd){
	cmd->lineEnd = 0;
	cmd->lock = initialSpinlock;
	cmd->listenerCount = 0;
	cmd->lineFIFO = createFIFO(1024, sizeof(uint8_t));
	if(cmd->lineFIFO == NULL){
		return 0;
	}
	return 1;
}

static void addCmdListenerCount(ConsoleCommandLine *cmd, int v){
	acquireLock(&cmd->lock);
	cmd->listenerCount += v;
	int newCount = cmd->listenerCount;
	releaseLock(&cmd->lock);
	if(newCount == 0){
		uint8_t a;
		while(readFIFONonBlock(cmd->lineFIFO, &a));
	}
}

// return 1 if need to continue iteration
static int processConsoleInput(ConsoleCommandLine *cmd){
	acquireLock(&cmd->lock);
	RWConsoleRequest *r = cmd->requestHead;
	if(r != NULL){
		REMOVE_FROM_DQUEUE(r);
	}
	releaseLock(&cmd->lock);
	EXPECT(r != NULL);
	uintptr_t readCount;
	for(readCount = 0; readCount < r->bufferSize; readCount++){
		if(readFIFONonBlock(cmd->lineFIFO, r->buffer + readCount) == 0){
			break;
		}
	}
	EXPECT(readCount > 0 || readCount == r->bufferSize);
	completeRWFileIO(r->rwfr, readCount, 0);
	DELETE(r);
	return 1;

	ON_ERROR;
	acquireLock(&cmd->lock);
	ADD_TO_DQUEUE(r, &cmd->requestHead);
	releaseLock(&cmd->lock);
	ON_ERROR;
	return 0;
}

static int backspaceHandler(ConsoleCommandLine *cmd){
	if(cmd->lineEnd > 0){
		cmd->lineEnd -= printkBackspace(1);
	}
	return cmd->lineEnd;
}

static int enterHandler(ConsoleCommandLine *cmd){
	cmd->line[cmd->lineEnd] = '\n';
	printkString("\n", 1);
	cmd->lineEnd++;

	acquireLock(&cmd->lock);
	int openCount = cmd->listenerCount;
	releaseLock(&cmd->lock);
	if(openCount == 0){
		parseCommand(cmd->line, cmd->lineEnd - 1);
	}
	else{
		int i;
		for(i = 0; i < cmd->lineEnd; i++){
			if(writeFIFO(cmd->lineFIFO, cmd->line + i) == 0){
				break;
			}
		}
		if(i < cmd->lineEnd){
			printk("warning: command line is truncated because buffer is full.\n");
		}
		while(processConsoleInput(cmd)){
		}
	}
	cmd->lineEnd = 0;
	return 0;
}

static int printHandler(ConsoleCommandLine *cmd, int key){
	if(cmd->lineEnd >= MAX_COMMAND_LINE_LENGTH - 1){
		return cmd->lineEnd;
	}
	if(key >= 256){
		key = '?';
	}
	cmd->line[cmd->lineEnd] = (key & 0xff);
	cmd->lineEnd += printkString(cmd->line + cmd->lineEnd, 1);
	return cmd->lineEnd;
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

static void kernelConsoleLoop(ConsoleCommandLine *cmd){
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
				backspaceHandler(cmd);
				break;
			case ENTER:
				enterHandler(cmd);
				break;
			default:
				printHandler(cmd, (kbState.shiftPressed? shiftKey(shiftKeyMap, ke.key): ke.key));
				break;
			}
		}
	}
	printk("cannot read ps/2 keyboard");
	systemCall_terminate();
}

typedef struct OpenedConsole{
	ConsoleDisplay *display;
	ConsoleCommandLine *commandLine;
}OpenedConsole;

static int readConsole(RWFileRequest *rwfr, OpenedFile *of, uint8_t *buffer, uintptr_t bufferSize){
	OpenedConsole *oc = getFileInstance(of);
	RWConsoleRequest *NEW(rwcr);
	if(rwcr == NULL){
		return 0;
	}
	rwcr->rwfr = rwfr;
	rwcr->buffer = buffer;
	rwcr->bufferSize = bufferSize;
	acquireLock(&oc->commandLine->lock);
	ADD_TO_DQUEUE(rwcr, &oc->commandLine->requestHead);
	releaseLock(&oc->commandLine->lock);
	while(processConsoleInput(oc->commandLine)){
	}
	return 1;
}

static int writeConsole(RWFileRequest *rwfr, OpenedFile *of, const uint8_t *buffer, uintptr_t bufferSize){
	OpenedConsole *oc = getFileInstance(of);
	printString(oc->display, (const char*)buffer, bufferSize);
	completeRWFileIO(rwfr, bufferSize, 0);
	return 1;
}

static void closeConsole(CloseFileRequest *cfr, OpenedFile *of){
	OpenedConsole *oc = getFileInstance(of);
	addCmdListenerCount(oc->commandLine, -1);
	DELETE(oc);
	completeCloseFile(cfr);
}

static int openConsole(OpenFileRequest *ofr, __attribute__((__unused__)) const char *fileName, uintptr_t nameLength, OpenFileMode ofm){
	if(nameLength != 0 || ofm.writable == 0){
		return 0;
	}
	OpenedConsole *NEW(oc);
	EXPECT(oc != NULL);
	oc->display = &kernelDisplay;
	oc->commandLine = &kernelCommand;
	addCmdListenerCount(oc->commandLine, 1);
	FileFunctions ff = INITIAL_FILE_FUNCTIONS;
	ff.read = readConsole;
	ff.write = writeConsole;
	ff.close = closeConsole;
	completeOpenFile(ofr, oc, &ff);
	return 1;
	//DELETE(oc);
	ON_ERROR;
	return 0;
}

void kernelConsoleService(void){
	if(initConsoleCommandLine(&kernelCommand) == 0){
		panic("cannot initialize kernel console");
	}
	FileNameFunctions fnf = INITIAL_FILE_NAME_FUNCTIONS;
	fnf.open = openConsole;
	int ok =addFileSystem(&fnf, "console", strlen("console"));
	if(!ok){
		panic("cannot initialize console service");
	}
	kernelConsoleLoop(&kernelCommand);
}

#ifndef NDEBUG
void testDelayEcho(void);
void testDelayEcho(void){
	int ok = waitForFirstResource("console", RESOURCE_FILE_SYSTEM, matchName);
	assert(ok);
	uintptr_t f = syncOpenFileN("console:", strlen("console:"), OPEN_FILE_MODE_WRITABLE);
	uintptr_t r;
	while(1){
		sleep(1000);
		char buf[8];
		const char *pre = "echo>";
		uintptr_t rCnt = sizeof(buf);
		r = syncReadFile(f, buf, &rCnt);
		assert(r != IO_REQUEST_FAILURE);
		uintptr_t wCnt = strlen(pre);
		r = syncWriteFile(f, pre, &wCnt);
		assert(r != IO_REQUEST_FAILURE && wCnt == (unsigned)strlen(pre));
		wCnt = rCnt;
		r = syncWriteFile(f, buf, &wCnt);
		assert(r != IO_REQUEST_FAILURE && wCnt == rCnt);
	}
	r = syncCloseFile(f);
	systemCall_terminate();
}

#endif
