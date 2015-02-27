#include"common.h"
#include"assembly/assembly.h"

// static_assert(sizeof(int8_t) == 1);
static_assert(sizeof(uint8_t) == 1);
// static_assert(sizeof(int16_t) == 2);
static_assert(sizeof(uint16_t) == 2);
// static_assert(sizeof(int32_t) == 4);
static_assert(sizeof(uint32_t) == 4);
// static_assert(sizeof(int64_t) == 8);
static_assert(sizeof(uint64_t) == 8);
static_assert(sizeof(uintptr_t) == sizeof(void*));

void *memset(void *ptr, unsigned char value, size_t size){
	size_t a;
	for(a = 0; a < size; a++){
		((uint8_t*)ptr)[a] = value;
	}
	return ptr;
}

void *memcpy(void *dst, const void *src, size_t size){
	size_t a;
	for(a = 0; a < size; a++){
		((uint8_t*)dst)[a] = ((uint8_t*)src)[a];
	}
	return dst;
}

int strlen(const char *s){
	int len;
	for(len = 0; s[len] != '\0'; len++);
	return len;
}

static int strcmp2(const char *s1, const char *s2, int n, int ncmp){
	int i = 0;
	for(i = 0; ncmp == 0 || i < n ; i++){
		if(s1[i] != s2[i])
			return s1[i] > s2[i]? 1: -1;
		if(s1[i] == '\0')
			return 0;
	}
	return 0;
}

int strncmp(const char *s1, const char *s2, int n){
	return strcmp2(s1, s2, n, 1);
}

int strcmp(const char *s1, const char *s2){
	return strcmp2(s1, s2, 0, 0);
}

static int printString(const char *s){
	int a;
	volatile uint16_t *const consoleVideo=(uint16_t*)0xb8000;
	static int globalCursor = 0;
	int cursor = globalCursor;
	static const int maxRow = 25, maxColumn=80;
	for(a = 0; s[a] != '\0'; a++){
		if(s[a]=='\n'){
			cursor = cursor - (cursor % maxColumn) + maxColumn;
		}
		else{
			consoleVideo[cursor] = s[a] + 0x0f00;
			cursor++;
		}
		if(cursor == maxRow * maxColumn){
			int b;
			for(b = 0; b < (maxRow - 1) * maxColumn; b++)
				consoleVideo[b] = consoleVideo[b + maxColumn];
			for(b = (maxRow - 1) * maxColumn; b < maxRow * maxColumn; b++)
				consoleVideo[b] = ' ' + 0x0f00;
			cursor -= maxColumn;
		}
	}
	globalCursor = cursor;
	return a;
}

static int printHexadecimal(unsigned n){
	char s[12];
	int a;
	s[8] = '\0';
	for(a = 7; a >= 0; a--){
		unsigned b = (n & 15);
		s[a] = (b >= 10? b - 10 + 'a': b + '0');
		n = (n >> 4);
	}
	return printString(s);
}

static int printUnsigned(unsigned n){
	char s[12];
	int a;
	a=11;
	s[a]='\0';
	do{
		a--;
		s[a] = (n % 10) + '0';
		n /= 10;
	}while(n != 0);
	return printString(s + a);
}

static int printSigned(int n){
	int printMinus = 0;
	if(n < 0){
		printString("-");
		printMinus = 1;
		n = -n;
	}
	return printMinus + printUnsigned((unsigned)n);
}

static int printCharacter(int c){
	char t[4];
	t[0] = (char)c;
	t[1] = '\0';
	return printString(t);
}

int kprintf(const char* format, ...){
	int printCount = 0;
	va_list argList;
	va_start(argList, format);
	int percentFlag = 0, formatLength = 0;
	int i;
	for(i = 0; format[i] != '\0'; i++){
		if(percentFlag == 0){
			if(format[i] == '%'){
				percentFlag = 1;
				formatLength = 0;
				continue;
			}
			char s[4];
			s[0] = format[i];
			s[1] = '\0';
			printCount += printString(s);
			continue;
		}
		if(format[i] >= '0' && format[i] <= '9'){
			formatLength = formatLength * 10 + format[i] - '0';
			continue;
		}
		percentFlag = 0;
		switch(format[i]){
		case '%':
			printCount += printString("%");
			break;
		case 'd':
			printCount += printSigned(va_arg(argList, int));
			break;
		case 'x':
			printCount += printHexadecimal(va_arg(argList, unsigned));
			break;
		case 'u':
			printCount += printUnsigned(va_arg(argList, unsigned));
			break;
		case 's':
			printCount += printString(va_arg(argList, const char*));
			break;
		case 'c':
			/*char is promoted to int when passed through ...*/
			printCount += printCharacter(va_arg(argList, int));
		}
		percentFlag = 0;
	}
	va_end(argList);
	return printCount;
}

void printAndHalt(const char *condition, const char *file, int line){
	kprintf("failure: %s %s %d", condition, file, line);
	while(1)
		hlt();
}
