#include"bios.h"
#include"assembly/assembly.h"
#include"memory/memory.h"
#include"io.h"
#include"task/task.h"
#include"interrupt/systemcalltable.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"common.h"
#include"kernel.h"
#include"fifo.h"

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
typedef struct Video{
	FIFO *biosFIFO;
}Video;
static Video video = {NULL};

static enum VBEFunction lastData = NO_FUNCTION;

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
	p->es8086 = (V8086_STACK_BOTTOM >> 4);
	p->regs.edi = (V8086_STACK_BOTTOM & 0xf);
	memcpy((void*)V8086_STACK_BOTTOM, "    ", 4);
}

static void getVBEInfo_out(__attribute__((__unused__)) InterruptParam *p){
	VBEInfo *vbeInfo = (VBEInfo*)V8086_STACK_BOTTOM;
	printk("VBE version = %x\n", p->regs.eax, vbeInfo->version);
	/* the virtual machines does not provide complete VBEInfo
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
	p->es8086 = (V8086_STACK_BOTTOM >> 4);
	p->regs.edi = (V8086_STACK_BOTTOM & 0xf);
}

static void getVBEModeInfo_out(__attribute__((__unused__)) InterruptParam *p){
	struct VBEModeInfo *info = (void*)V8086_STACK_BOTTOM;
	printk("granularity = %u KB\n", info->windowGranularity);
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
	p->regs.ebx = /*set*/(0 << 8) + /*0 for window A; 1 for window B*/0;
	p->regs.edx = 0; // window number
}

static void setVBEDisplayWindow_out(__attribute__((__unused__)) InterruptParam *p){
}

static void setVBEDisplayStart_in(InterruptParam *p){
	p->regs.eax = 0x4f07;
	p->regs.ebx = 0;
	p->regs.ecx = 0; // first pixel
	p->regs.edx = 0; // first scan line
}
static void setVBEDisplayStart_out(InterruptParam *p){
	printk("setVBEDisplayStart: %x\n",p->regs.eax);
	int a;
	for(a=0;a<65536;a++){
		((volatile uint8_t*)0xa0000)[a]=(a%3==2?0xff:0);
	}
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

static void setVBEArgument(InterruptParam *p){
	sti();
	Video *v = (Video*)(p->argument);
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
		readFIFO(v->biosFIFO, &lastData);
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

static void syscall_video(InterruptParam *p){
	Video *v = (Video*)(p->argument);
	uintptr_t param0 = SYSTEM_CALL_ARGUMENT_0(p);
	if(writeFIFO(v->biosFIFO, &param0) == 0){
		SYSTEM_CALL_RETURN_VALUE_0(p) = 1;
	}
}

// see virtual8086.asm
void callBIOS(void);

void vbeDriver(void){
	video.biosFIFO = createFIFO(8, sizeof(uintptr_t));
	setTaskSystemCall(processorLocalTask(), setVBEArgument, (uintptr_t)&video);
	uintptr_t data = GET_VBE_INFO;
	writeFIFO(video.biosFIFO, &data);
	data = GET_VBE_MODE_INFO;
	writeFIFO(video.biosFIFO, &data);
	//data = SET_VBE_MODE
	//writeFIFO(video.biosFIFO, &data);
	data = SET_VBE_DISPLAY_WINDOW;
	writeFIFO(video.biosFIFO, &data);
	data = SET_VBE_DISPLAY_START;
	writeFIFO(video.biosFIFO, &data);
	registerService(global.syscallTable, VIDEO_SERVICE_NAME, syscall_video, (uintptr_t)&video);

	if(switchToVirtual8086Mode(callBIOS) == 0){
		terminateCurrentTask();
	}
	panic("switch to v8086 error");
}
