#include"interrupt/handler.h"
#include"assembly/assembly.h"
#include"interrupt/systemcall.h"
#include"interrupt/controller/pic.h"
#include"multiprocessor/processorlocal.h"
#include"io.h"
#include"task/task.h"
#include"common.h"
#include"fifo.h"

static void mouseInput(uint8_t newData){
	static int state = 0;
	static uint8_t prev0, data[3];
	data[state] = newData;
	if(state < 2){
		state++;
		return;
	}

	int dx = (char)data[1], dy = (char)data[2], pressed = 0, released = 0;
	pressed = (data[0] & 7 & (data[0] ^ prev0));
	released = (prev0 & 7 & (data[0] ^ prev0));
	if(data[0] & 64){ // x overflow
		dx = (data[0] & 16? -128: 128);
	}
	if(data[0] & 128){ // y overflow
		dy = (data[0] & 32? -128: 128);
	}
	printk("%x %x %d %d\n", pressed, released, dx,dy);
	prev0 = data[0];
	state = 0;
}
enum SpecialKey{
	NO_KEY = 0,
	BACKSPACE = 8,
	ENTER = '\n',
	ESC = 27,
	CAPS_LOCK = 256,
	SCROLL_LOCK,
	NUM_LOCK,
	INSERT,
	BREAK,
	HOME, END,
	PAGE_UP, PAGE_DOWN,
	UP, DOWN, LEFT, RIGHT,
	F1, F2, F3, F4, F5, F6,
	F7, F8, F9, F10, F11, F12,
	LEFT_SHIFT, RIGHT_SHIFT,
	LEFT_CTRL, RIGHT_CTRL,
	LEFT_ALT, RIGHT_ALT,
	LEFT_GUI, RIGHT_GUI,
	APP,
	PAD_ASTERISK, PAD_DIVIDE, PAD_PLUS, PAD_MINUS,
	PAD_0, PAD_1, PAD_2, PAD_3, PAD_4,
	PAD_5, PAD_6, PAD_7, PAD_8, PAD_9,
	PAD_ENTER, PAD_DOT,
	PRINT_SCREEN,
	PAUSE
};
static const unsigned short scan1[][2] = {
	{ESC, 0x01}, {F1, 0x3b}, {F2, 0x3c}, {F3, 0x3d}, {F4, 0x3e},
	{F5, 0x3f}, {F6, 0x40}, {F7, 0x41}, {F8, 0x42}, {F9, 0x43},
	{F10, 0x44}, {F11, 0x57}, {F12, 0x58}, {SCROLL_LOCK, 0x46},
	{'`', 0x29}, {'1', 0x02}, {'2', 0x03}, {'3', 0x04}, {'4',0x05},
	{'5', 0x06}, {'6', 0x07}, {'7', 0x08}, {'8',0x09}, {'9', 0x0a},
	{'0', 0x0b}, {'-', 0x0c}, {'=', 0x0d}, {BACKSPACE, 0x0e},
	{'\t',0x0f}, {'q', 0x10}, {'w', 0x11}, {'e', 0x12}, {'r',0x13},
	{'t', 0x14}, {'y', 0x15}, {'u', 0x16}, {'i',0x17}, {'o', 0x18},
	{'p', 0x19}, {'[', 0x1a}, {']', 0x1b}, {'\\',0x2b},
	{CAPS_LOCK, 0x3a}, {'a', 0x1e}, {'s', 0x1f}, {'d', 0x20}, {'f', 0x21},
	{'g', 0x22}, {'h', 0x23}, {'j', 0x24}, {'k', 0x25}, {'l', 0x26},
	{';', 0x27}, {'\'',0x28}, {ENTER,0x1c},
	{LEFT_SHIFT, 0x2a}, {'z', 0x2c}, {'x', 0x2d}, {'c', 0x2e}, {'v', 0x2f},
	{'b', 0x30}, {'n', 0x31}, {'m', 0x32}, {',', 0x33}, {'.', 0x34},
	{'/', 0x35}, {RIGHT_SHIFT, 0x36},
	{LEFT_CTRL, 0x1d}, {LEFT_ALT, 0x38}, {' ', 0x39},
	{NUM_LOCK, 0x45},
	{PAD_ASTERISK, 0x37}, {PAD_MINUS, 0x4a}, {PAD_PLUS, 0x4e}, {PAD_DOT, 0x53},
	{PAD_0, 0x52}, {PAD_1, 0x4f}, {PAD_2, 0x50}, {PAD_3, 0x51}, {PAD_4, 0x4b},
	{PAD_5, 0x4c}, {PAD_6, 0x4d}, {PAD_7, 0x47}, {PAD_8, 0x48}, {PAD_9, 0x49},
	{'\0', 0}
};
static unsigned short scan1ToKey[256];
static const unsigned short scanE0[][2] = {
	{HOME, 0x47}, {END, 0x4f}, {PAGE_UP, 0x49}, {PAGE_DOWN, 0x51},
	{UP, 0x48}, {DOWN, 0x50}, {LEFT, 0x4b}, {RIGHT, 0x4d},
	{PAD_DIVIDE, 0x35}, {PAD_ENTER, 0x1c},
	{RIGHT_ALT, 0x38}, {RIGHT_CTRL, 0x1d},
	{INSERT, 0x52}, {BREAK, 0x53},
	{LEFT_GUI, 0x5b}, {RIGHT_GUI, 0x5c},
	{APP, 0x5d},
	{'\0', 0}
};
static unsigned short scanE0ToKey[256];
// PRINT_SCREEN
// PAUSE
#define NUMBER_OF_SPECIAL_SCAN (3)
static const struct{
	unsigned short key;
	unsigned char released, length;
	uint8_t scanCode[6];
}scanSpecial[NUMBER_OF_SPECIAL_SCAN] = {
	{PRINT_SCREEN, 0, 4, {0xe0, 0x2a, 0xe0, 0x37, 0, 0}},
	{PRINT_SCREEN, 1, 4, {0xe0, 0xb7, 0xe0, 0xaa, 0, 0}},
	{PAUSE, 0, 6, {0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5}}
};

static unsigned int scanCodeToKey(uint8_t scanCode, int *released){
	static int e0Flag, sp[NUMBER_OF_SPECIAL_SCAN];
	unsigned short key = NO_KEY, re = 0;
	int i;
	for(i = 0; i < NUMBER_OF_SPECIAL_SCAN; i++){
		if(scanSpecial[i].scanCode[sp[i]] != scanCode){
			sp[i] = 0;
			continue;
		}
		sp[i]++;
		if(sp[i] == scanSpecial[i].length){
			key = scanSpecial[i].key;
			re = scanSpecial[i].released;
		}
	}
	if(key == NO_KEY && sp[2] == 0/*scanSpecial[2] = PAUSE*/){
		if(e0Flag == 0 && scanCode == 0xe0){
			e0Flag = 1;
		}
		else{
			key = (e0Flag == 1? scanE0ToKey[scanCode & 0x7f]: scan1ToKey[scanCode & 0x7f]);
			re = ((scanCode >> 7) & 1);
		}
	}
	if(key == NO_KEY)
		return key;
	e0Flag = 0;
	for(i = 0; i < NUMBER_OF_SPECIAL_SCAN; i++){
		sp[i] = 0;
	}
	*released = re;
	return key;
}

#define PS2_DATA_PORT (0x60)
#define PS2_CMD_PORT (0x64)

#define READABLE_FLAG (1)
#define WRITABLE_FLAG (2)
#define DATA_FROM_MOUSE_FLAG (32)
static int isReadable(void){
	return in8(PS2_CMD_PORT) & READABLE_FLAG;
}

static uint8_t readData(void){
	return in8(PS2_DATA_PORT);
}

static void writeData(uint16_t port, uint8_t data){
	while(in8(PS2_CMD_PORT) & WRITABLE_FLAG);
	out8(port, data);
}

typedef struct PS2Data{
	FIFO *intFIFO, *sysFIFO;
}PS2Data;

// ps2Handler -> ps2Driver -> keyboardInput ->syscall_keyboard

static void ps2Handler(InterruptParam *p){
	uintptr_t status = in8(PS2_CMD_PORT);
	if((status & READABLE_FLAG) != 0){
		uintptr_t data = readData();
		PS2Data *ps2 = (PS2Data*)(p->argument);
		writeFIFO(ps2->intFIFO, (status | (data << 8)));
	}
	processorLocalPIC()->endOfInterrupt(p);
	sti();
}

static void syscall_keyboard(InterruptParam *p){
	PS2Data *ps2Data = (PS2Data*)(p->argument);
	uintptr_t key;
	if(readFIFO(ps2Data->sysFIFO, &key) == 0){
		printk("warning: cannot read keyboard fifo");
		key = NO_KEY;
	}
	SYSTEM_CALL_RETURN_VALUE_0(p) = key;
}

static void syscall_mouse(InterruptParam *p){
	printk("%d\n",p->regs.eax);
}

static void keyboardInput(uint8_t data, PS2Data *ps2Data){
	int release = 0;
	unsigned int key = scanCodeToKey(data, &release);
	if(key != NO_KEY && key < 128 && release == 0){
		overwriteFIFO(ps2Data->sysFIFO, key);
	}
}

static void initMouse(void){
	uint8_t conf = 0x47;
	writeData(PS2_CMD_PORT, 0x20);
	while(isReadable() == 0);
	conf = ((readData() | (1 << 1)) & ~(1 << 5));
	while(isReadable()){
		readData();
	}
	writeData(PS2_CMD_PORT, 0x60);
	writeData(PS2_DATA_PORT, conf);
	writeData(PS2_CMD_PORT, 0xd4);
	writeData(PS2_DATA_PORT, 0xf4);
	while(isReadable()){
		readData(); // 0xfa
	}
}

static void initKeyboard(void){
	int i;
	for(i = 0; i < 256; i++){
		scan1ToKey[i] = NO_KEY;
		scanE0ToKey[i] = NO_KEY;
	}
	for(i = 0; scan1[i][0] != 0; i++){
		scan1ToKey[scan1[i][1]] = scan1[i][0];
	}
	for(i = 0; scanE0[i][0] != 0; i++){
		scanE0ToKey[scanE0[i][1]] = scanE0[i][0];
	}
}

static PS2Data ps2 = {NULL, NULL};

typedef struct InterruptController PIC;
typedef struct SystemCallTable SystemCallTable;

static void initPS2Driver(PIC* pic, SystemCallTable *syscallTable){
	// interrupt
	InterruptVector *keyboardVector = pic->irqToVector(pic, KEYBOARD_IRQ);
	InterruptVector *mouseVector = pic->irqToVector(pic, MOUSE_IRQ);
	initMouse();
	initKeyboard();
	ps2.intFIFO = createFIFO(64);
	ps2.sysFIFO = createFIFO(128);

	setHandler(mouseVector, ps2Handler, (uintptr_t)&ps2);
	setHandler(keyboardVector, ps2Handler, (uintptr_t)&ps2);
	pic->setPICMask(pic, MOUSE_IRQ, 0);
	pic->setPICMask(pic, KEYBOARD_IRQ, 0);
	// system call
	registerSystemService(syscallTable, KEYBOARD_SERVICE_NAME, syscall_keyboard, (uintptr_t)&ps2);
	registerSystemService(syscallTable, MOUSE_SERVICE_NAME, syscall_mouse, (uintptr_t)&ps2);
}

void ps2Driver(void){
	initPS2Driver(processorLocalPIC(), global.syscallTable);
	while(1){
		uintptr_t ds;
		uint8_t data, status;
		if(readFIFO(ps2.intFIFO, &ds) == 0){
			panic("ps2 intFIFO");
		}
		data = ((ds >> 8) & 0xff);
		status = (ds & 0xff);
		if(status & DATA_FROM_MOUSE_FLAG){
			mouseInput(data);
		}
		else{
			keyboardInput(data, &ps2);
		}
	}
}
