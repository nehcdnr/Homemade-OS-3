#include <io/bios.h>
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"io.h"
#include"task/task.h"
#include"multiprocessor/spinlock.h"
#include"common.h"
#include"fifo.h"

struct ConsoleDisplay{
	int cursor;
	int maxRow, maxColumn;
	volatile uint16_t *video;
	Spinlock lock;
};

#define DEFAULT_TEXT_VIDEO_ADDRESS ((volatile uint16_t*)0xb8000)
ConsoleDisplay kernelConsole = {
	0,
	25, 80,
	DEFAULT_TEXT_VIDEO_ADDRESS,
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

int printString(const char *s);
int printString(const char *s){
	int a;
	acquireLock(&kernelConsole.lock); //TODO: 1 lock&unlock per kprintf
	a = printConsole(&kernelConsole, s);
	releaseLock(&kernelConsole.lock);
	return a;
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

int printConsole(ConsoleDisplay *cd, const char *s){
	const int rc = cd->maxColumn * cd->maxRow;
	int a;
	for(a = 0; s[a] != '\0'; a++){
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

ConsoleDisplay *initKernelConsole(void){
	static int needInit = 1;
	ConsoleDisplay *cd = &kernelConsole;
	if(needInit){
		needInit = 0;
		cd->cursor = 0;
		cd->maxRow = 25;
		cd->maxColumn = 80;
		cd->video = DEFAULT_TEXT_VIDEO_ADDRESS;
		cd->lock = initialSpinlock;
		updateVideoAddress(cd);
		updateCursor(cd);
		int c = 0;
		for(c = 0; c < cd->maxColumn * cd->maxRow; c++){
			cd->video[c] = WHITE_SPACE;
		}
	}
	return cd;
}

/*
few (virtual) machines support VBE 3.0
typedef struct{
	char signature[4];
	uint16_t entryPoint;
	uint16_t pnInitialize;
	uint16_t biosDataSelecor;
	uint16_t a0000Selector;
	uint16_t b0000Selector;
	uint16_t b8000Selector;
	uint16_t codeSelector;
	uint8_t isProtected;
	uint8_t checksum;
}PMInfoBlock;

static_assert(sizeof(PMInfoBlock) == 20);

#define BIOS_IMAGE_START (0xc0000)
#define BIOS_IMAGE_END (BIOS_IMAGE_START + 32768)

void initVideo(void){
	PMInfoBlock *pmib =
	searchStructure(sizeof(PMInfoBlock), "PMID", 0, 1<<20);
	if(pmib == NULL){
		printk("BIOS does not support VBE 3.0\n");
		return;
	}
	printk("%x %x %x %x\n",pmib->entryPoint, pmib->pnInitialize, pmib->isProtected, pmib);
}
*/

enum VBEFunction{
	GET_VBE_INFO,
	GET_VBE_MODE_INFO,
	SET_VBE_MODE,
	GET_VBE_MODE,
	GET_VBE_DISPLAY_WINDOW,
	SET_VBE_DISPLAY_WINDOW,
	SET_VBE_DISPLAY_START,
	NO_FUNCTION,
	NUMBER_OF_VBE_FUNCTIONS
};

// TODO: paging & dynamic allocate stack
#define V8086_STACK_TOP (0x4000)
static FIFO *biosFIFO = NULL;
enum VBEFunction lastData = NO_FUNCTION;

void callBIOS(void);
static void startVBETask(void){
	startVirtual8086Task(callBIOS, V8086_STACK_TOP - 4);
}

// VBE 2.0
typedef struct{
	uint8_t signature[4];
	uint16_t version;
	uint32_t oemString;
	uint8_t capabilities[4];
	uint32_t videoModePtr;
	uint16_t numberOf64KBBlocks;
	uint16_t softwareRevision;
	uint32_t vendorNameString;
	uint32_t productNameString;
	uint8_t reserved[222];
	uint8_t oemStringData[256];
}VBEInfo;

static_assert(sizeof(VBEInfo) == 512);

static void getVBEInfo_in(InterruptParam *p){
	p->regs.eax = 0x4f00;
	p->es8086 = (V8086_STACK_TOP >> 4);
	p->regs.edi = (V8086_STACK_TOP & 0xf);
	memcpy((void*)V8086_STACK_TOP, "    ", 4);
}

static void getVBEInfo_out(__attribute__((__unused__)) InterruptParam *p){
	VBEInfo *vbeInfo = (VBEInfo*)V8086_STACK_TOP;
	printk("VBE version = %x\n", p->regs.eax, vbeInfo->version);
	/* the virtual machines does not provide complete VBEInfo
	printk("%s %x\n", vbeInfo->signature, vbeInfo->version);
	uint16_t * modePtr;
	for(modePtr = (uint16_t*)vbeInfo->videoModePtr; *modePtr != 0 && *modePtr != 0xffff; modePtr++){
		printk("%x\n", *modePtr);
	}
	*/
}

enum VBE_MODE{
	_320_200 = 0x10f,
	_640_480 = 0x112,
	_800_600 = 0x115,
	_1024_768 = 0x118,
	_1280_1024 = 0x11b
};
#define TEST_MODE_NO (_800_600)

struct VBEModeInfo{
	uint16_t modeAttributes;
	uint8_t windowAAttributes;
	uint8_t windowBAttributes;
	uint16_t windowGranularity;
	uint16_t windowSize;
	uint16_t windowAStartSegment;
	uint16_t windowBStartSegment;
	uint32_t windowFunction;
	uint16_t bytesPerScanLine;
	// VBE1.2
	uint16_t xResolution; // horizontal
	uint16_t yResolution; // vertical
	uint8_t xCharSize;
	uint8_t yCharSize;
	uint8_t numberOfPlanes;
	uint8_t bitsPerPixel;
	uint8_t numberOfBanks;
	uint8_t memoryModel;
	uint8_t bankSize; // in KB
	uint8_t numberOfImagePages;
	uint8_t reserved;
	// for direct/6 YUV/7 models
	uint8_t redMaskSize;
	uint8_t redFieldPosition;
	uint8_t greenMaskSize;
	uint8_t greenFieldPosition;
	uint8_t blueMaskSize;
	uint8_t blueFieldPosition;
	uint8_t rsvdMaskSize;
	uint8_t rsvdFieldPosition;
	uint8_t DirectColorModeInfo;
	// VBE 2.0
	uint32_t physicalBasePtr;
	uint32_t offScreenMemOffset;
	uint16_t offScreenMemSize; // KB
	uint8_t reserved2[206];
};

static_assert(sizeof(struct VBEModeInfo) == 256);

static void getVBEModeInfo_in(InterruptParam *p){
	p->regs.eax = 0x4f01;
	p->regs.ecx = TEST_MODE_NO;
	p->es8086 = (V8086_STACK_TOP >> 4);
	p->regs.edi = (V8086_STACK_TOP & 0xf);
}

static void getVBEModeInfo_out(__attribute__((__unused__)) InterruptParam *p){
	struct VBEModeInfo *info = (void*)V8086_STACK_TOP;
	printk("window A segment = %x\n", info->windowAStartSegment);
	printk("window B segment = %x\n", info->windowBStartSegment);
	printk("VBE flat model memory mapping = %x\n", info->physicalBasePtr);
}

static void setVBEMode_in(InterruptParam *p){
	p->regs.eax = 0x4f02;
	p->regs.ebx = (TEST_MODE_NO |
	/*flat frame buffer*/ (0 << 14) |
	/*not clear buffer*/(1 << 15));
}

static void setVBEMode_out(__attribute__((__unused__)) InterruptParam *p){
}

static void setVBEDisplayWindow_in(InterruptParam *p){
	p->regs.eax = 0x4f05;
	p->regs.ebx = /*set*/(0 << 8) + /*window A*/0;
	p->regs.edx = 0; // window number
}

static void setVBEDisplayWindow_out(__attribute__((__unused__)) InterruptParam *p){
	int a;
	for(a=0;a<65536;a++)((uint8_t*)0xa0000)[a]=(a%3==1?0xff:0);
}

static void setVBEDisplayStart_in(InterruptParam *p){
	p->regs.eax = 0x4f07;
	p->regs.ebx = 0;
	p->regs.ecx = 0;
	p->regs.edx = 0;
}
static void setVBEDisplayStart_out(InterruptParam *p){
	printk("setVBEDisplayStart: %x %x",p->regs.ecx, p->regs.edx);
}

static void noVBEFunction(__attribute__((__unused__)) InterruptParam *p){
}

static const struct{
	enum VBEFunction number;
	void (*input)(InterruptParam*);
	void (*output)(InterruptParam*);
}vbeFunctions[NUMBER_OF_VBE_FUNCTIONS] = {
	{GET_VBE_INFO, getVBEInfo_in, getVBEInfo_out},
	{GET_VBE_MODE_INFO, getVBEModeInfo_in, getVBEModeInfo_out},
	{SET_VBE_MODE, setVBEMode_in, setVBEMode_out},
	{GET_VBE_MODE, noVBEFunction, noVBEFunction},
	{GET_VBE_DISPLAY_WINDOW, noVBEFunction, noVBEFunction},
	{SET_VBE_DISPLAY_WINDOW, setVBEDisplayWindow_in, setVBEDisplayWindow_out},
	{SET_VBE_DISPLAY_START, setVBEDisplayStart_in, setVBEDisplayStart_out},
	{NO_FUNCTION, noVBEFunction, noVBEFunction}
};

static void setVBEParameter(InterruptParam *p){
	p->regs.eax = (p->regs.ebx >> 16);
	p->regs.ebx &= 0xffff;
	// read last result
	if(((p->regs.eax >> 8) & 0xff) != 0 || (p->regs.eax & 0xff) != 0x4f){
		if(lastData != NO_FUNCTION){
			printk("VBE function %x failed; return value = %x\n",
			lastData,
			p->regs.eax);
		}
	}
	else{
		int f;
		for(f = 0; f < NUMBER_OF_VBE_FUNCTIONS; f++){
			if(lastData == vbeFunctions[f].number){
				break;
			}
		}
		if(f < NUMBER_OF_VBE_FUNCTIONS){
			vbeFunctions[f].output(p);
		}
		else{
			assert(0);
		}
	}
	while(1){
		// wait for next request
		while(readFIFO(biosFIFO, &lastData) == 0){
			systemCall(SYSCALL_SUSPEND);
		}
		// set next argument
		int f;
		for(f = 0; f < NUMBER_OF_VBE_FUNCTIONS; f++){
			if(lastData == vbeFunctions[f].number){
				break;
			}
		}
		if(f == NUMBER_OF_VBE_FUNCTIONS){
			printk("VBE: unknown data: %d\n", lastData);
			lastData = NO_FUNCTION;
			continue;
		}
		else{
			vbeFunctions[f].input(p);
			break;
		}
	}
}

void initVideoTask(void){
	static int needInit =1;
	if(needInit == 0){
		return;
		panic("initBIOSTask");
	}
	needInit = 0;
	biosFIFO = createFIFO(32);
	Task *t = createKernelTask(startVBETask);
	setTaskSystemCall(t, setVBEParameter);
	writeFIFO(biosFIFO, GET_VBE_INFO);
	writeFIFO(biosFIFO, GET_VBE_MODE_INFO);
	writeFIFO(biosFIFO, SET_VBE_MODE);
	writeFIFO(biosFIFO, SET_VBE_DISPLAY_WINDOW);
	//writeFIFO(biosFIFO, SET_VBE_DISPLAY_START);
	resume(t);
}

