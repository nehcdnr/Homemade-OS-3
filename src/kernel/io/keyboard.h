

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

typedef struct{
	uint16_t key;
	uint16_t isRelease;
}KeyboardEvent;

typedef struct{
	signed short moveX, moveY;
	uint8_t pressLeft: 1;
	uint8_t pressRight: 1;
	uint8_t pressMiddle: 1;
	uint8_t releaseLeft: 1;
	uint8_t releaseRight: 1;
	uint8_t releaseMiddle: 1;
}MouseEvent;
