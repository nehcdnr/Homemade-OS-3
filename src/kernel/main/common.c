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

static int strcmp2(const char *s1, const char *s2, size_t n, int ncmp){
	size_t i = 0;
	for(i = 0; ncmp == 0 || i < n ; i++){
		if(s1[i] != s2[i])
			return s1[i] > s2[i]? 1: -1;
		if(s1[i] == '\0')
			return 0;
	}
	return 0;
}

int strncmp(const char *s1, const char *s2, size_t n){
	return strcmp2(s1, s2, n, 1);
}

int strcmp(const char *s1, const char *s2){
	return strcmp2(s1, s2, 0, 0);
}

char *strncpy(char *dst, const char *src, size_t n){
	size_t a;
	for(a = 0; a < n; a++){
		dst[a] = src[a];
		if(dst[a] == '\0'){
			break;
		}
	}
	return dst;
}

int tolower(int c){
	return (c >= 'A' && c <= 'Z'? c - 'A' + 'a': c);
}

int toupper(int c){
	return (c >= 'a' && c <= 'z'? c - 'a' + 'A': c);
}

int printString(const char *s, size_t length);

static int unsignedToString(char *str, unsigned number, int base){
	int i = 0;
	unsigned n2 = number;
	do{
		n2 = n2 / base;
		i++;
	}while(n2 != 0);
	n2 = number;
	int len = i;
	for(i--; i >= 0; i--){
		int c = n2 % base;
		str[i] = (c < 10? c + '0': c - 10 + 'a');
		n2 /= base;
	}
	return len;
}

static int signedToString(char *str, int number, int base){
	int printMinus = 0;
	if(number < 0){
		str[0] = '-';
		printMinus = 1;
		number = -number;
	}
	return printMinus + unsignedToString(str + printMinus, (unsigned)number, base);
}


uintptr_t indexOf(const char *s, uintptr_t i, uintptr_t len, char c){
	while(i < len && s[i] != c)
		i++;
	return i;
}

uintptr_t indexOfNot(const char *s, uintptr_t i, uintptr_t len, char c){
	while(i < len && s[i] == c)
		i++;
	return i;
}

// if we
#define PRINT_BUFFER_SIZE (sizeof(uintptr_t) * 8 + 4)

struct PrintfArg{
	int width;
	char pad0;
	char lCount;
	char base;
	char format;
	union{
		char c;
		int i;
		unsigned u;
		char *s;
		long long int lli;
		unsigned long long int llu;
	};
};

#define LLD ((('l' << 8) + 'l') << 8) + 'd')
#define LLU ((('l' << 8) + 'l') << 8) + 'u')

static const char *parsePrintfArg(struct PrintfArg*pa, const char *format){
	pa->width = 0;
	pa->pad0 = 0;
	pa->lCount = 0;
	pa->base = 0;
	if(*format != '%'){
		pa->format = '\0';
		pa->c = *format;
		return format + (*format != '\0'? 1: 0);
	}
	format++;
	while(*format == '0'){
		pa->pad0 = 1;
		format++;
	}
	while(*format >= '0' && *format <= '9'){
		pa->width = pa->width * 10 + (*format - '0');
		format++;
	}
	while(*format == 'l'){
		pa->lCount++;
		format++;
	}
	switch(*format){
	case 'b':
		pa->base = 2;
		break;
	case 'o':
		pa->base = 8;
		break;
	case 'u':
		pa->base = 10;
		break;
	case 'x':
		pa->base = 16;
		break;
	case 'd':
		pa->base = 10;
		break;
	}
	pa->format = *format;
	return format + (*format != '\0'? 1: 0);
}

#define SET_PRINTF_ARG(A, VA_LIST)\
do{\
	switch((A)->format){\
	case 'c':\
		/*char is promoted to int when passed through*/\
		(A)->c = (char)va_arg((VA_LIST), int);\
		break;\
	case 's':\
		(A)->s = va_arg((VA_LIST), char*);\
		break;\
	case 'd':\
		if((A)->lCount >=2)\
			(A)->lli = va_arg((VA_LIST), long long);\
		else\
			(A)->i = va_arg((VA_LIST), int);\
		break;\
	case 'b':\
	case 'o':\
	case 'u':\
	case 'x':\
		if((A)->lCount >=2)\
			(A)->llu = va_arg((VA_LIST), unsigned long long);\
		else\
			(A)->u = va_arg((VA_LIST), int);\
		break;\
	}\
}while(0)

// *bufferPtr is at least PRINT_BUFFER_SIZE
static int outputPrintfArg(char **bufferPtr, const struct PrintfArg *pa){
	char *buffer = *bufferPtr;
	int bufferLength;
	switch(pa->format){
	case '%':
		buffer[0] = '%';
		bufferLength = 1;
		break;
	case 's':
		buffer = *bufferPtr = pa->s;
		bufferLength = strlen(buffer);
		break;
	case 'd':
		bufferLength = signedToString(buffer, pa->i, (int)pa->base);
		break;
	case 'b':
	case 'o':
	case 'u':
	case 'x':
		bufferLength = unsignedToString(buffer, pa->u, (int)pa->base);
		break;
	case 'c':
	default:
		buffer[0] = pa->c;
		bufferLength = 1;
	}
	return bufferLength;
}

int sprintf(char *str, const char *format, ...){
	int printCount = 0;
	va_list argList;
	va_start(argList, format);
	while(*format != '\0'){
		char numberBuffer[PRINT_BUFFER_SIZE];
		char *buffer = numberBuffer;
		struct PrintfArg pa;
		format = parsePrintfArg(&pa, format);
		SET_PRINTF_ARG(&pa, argList);
		int bufferLength = outputPrintfArg(&buffer, &pa);
		strncpy(str + printCount, buffer, bufferLength);
		printCount += bufferLength;
	}
	va_end(argList);
	return printCount;
}

int printk(const char* format, ...){
	int printCount = 0;
	va_list argList;
	va_start(argList, format);
	while(*format != '\0'){
		char numberBuffer[PRINT_BUFFER_SIZE];
		char *buffer = numberBuffer;
		struct PrintfArg pa;
		format = parsePrintfArg(&pa, format);
		SET_PRINTF_ARG(&pa, argList);
		int bufferLength = outputPrintfArg(&buffer, &pa);
		printString(buffer, bufferLength);
		printCount += bufferLength;
	}
	va_end(argList);
	return printCount;
}

uintptr_t parseHexadecimal(const char *s, uintptr_t length){
	uintptr_t i, r = 0;
	for(i = 0; i < length; i++){
		r = r * 16 + (s[i] >= '0' && s[i] <= '9'? s[i] - '0': s[i] - 'a' + 10);
	}
	return r;
}

void printAndHalt(const char *condition, const char *file, int line){
	printk("failure: %s %s %d", condition, file, line);
	// if the console does not display, use vm debugger to watch registers
	cli();
	while(1){
		__asm__(
		"subl $0xf000be00, %1\n"
		"subl $0xf000be00, %2\n"
		:
		:"a"(line),"b"(file),"c"(condition)
		);
		hlt();
	}
}
