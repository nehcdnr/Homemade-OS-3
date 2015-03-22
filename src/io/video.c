#include <io/bios.h>
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
	acquireLock(kernelConsole.lock); //TODO: 1 lock&unlock per kprintf
	a = printConsole(&kernelConsole, s);
	releaseLock(kernelConsole.lock);
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

ConsoleDisplay *initKernelConsole(MemoryManager *m){
	static int needInit = 1;
	ConsoleDisplay *cd = &kernelConsole;
	if(needInit){
		needInit = 0;
		cd->cursor = 0;
		cd->maxRow = 25;
		cd->maxColumn = 80;
		cd->video = DEFAULT_VEDIO_ADDRESS;
		cd->lock = createSpinlock(m);
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
