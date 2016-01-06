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

int isspace(int c){
	switch(c){
	case ' ':
	case '\t':
	case '\n':
	case '\v':
	case '\f':
	case '\r':
		return 1;
	default:
		return 0;
	}
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


static int stringToUnsigned(const char *buffer, unsigned int *u, int base){
	int length;
	*u = 0;
	for(length = 0; 1; length++){
		const char b = buffer[length];
		int d = base;
		if(b >= 'A' && b <= 'Z')
			d = b - 'A' + 10;
		if(b >= 'a' && b <= 'z')
			d = b - 'a' + 10;
		else if(b >= '0' && b <= '9')
			d = b - '0';
		if(d >= base)
			break;
		(*u) = (*u) * base + d;
	}
	return length == 0? -1: length;
}

static int stringToSigned(const char *buffer, int *i, int base){
	int length = 0;
	int s = 1;
	if(*buffer == '+'){
		s = 1;
		length = 1;
	}
	else if(*buffer == '-'){
		s = -1;
		length = 1;
	}
	else{
		length = 0;
	}
	unsigned u;
	int length2 = stringToUnsigned(buffer + length, &u, base);
	if(length2 < 0)
		return -1;
	*i = ((int)u) * s;
	return length + length2;
}

int isStringEqual(const char *s1, uintptr_t len1, const char *s2, uintptr_t len2){
	if(len1 != len2){
		return 0;
	}
	return strncmp(s1, s2, len1) == 0;
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

		char *cp;
		int *ip;
		//unsigned *up;
		long long int *llip;
		//unsigned long long int *llup;
	};
};

#define LLD ((('l' << 8) + 'l') << 8) + 'd')
#define LLU ((('l' << 8) + 'l') << 8) + 'u')

// support format: %csdboux
#define NOT_FORMAT_SPECIFIER ((char)('\0'))

static const char *parsePrintfArg(const char *format, struct PrintfArg *pa){
	pa->width = 0;
	pa->pad0 = 0;
	pa->lCount = 0;
	pa->base = 0;
	if(*format != '%'){
		pa->format = NOT_FORMAT_SPECIFIER;
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
	case 'd':
		pa->base = 10;
		break;
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
			(A)->lli = va_arg((VA_LIST), long long int);\
		else\
			(A)->i = va_arg((VA_LIST), int);\
		break;\
	case 'b':\
	case 'o':\
	case 'u':\
	case 'x':\
		if((A)->lCount >=2)\
			(A)->llu = va_arg((VA_LIST), unsigned long long int);\
		else\
			(A)->u = va_arg((VA_LIST), unsigned int);\
		break;\
	}\
}while(0)

#define SET_SCANF_ARG(A, VA_LIST, SCAN) \
do{\
	(SCAN) = 1;\
	switch((A)->format){\
	case 'c':\
		(A)->cp = va_arg((VA_LIST), char*);\
		break;\
	case 's':\
		(A)->cp = va_arg((VA_LIST), char*);\
		break;\
	case 'd':\
	case 'b':\
	case 'o':\
	case 'u':\
	case 'x':\
		if((A)->lCount >= 2)\
			(A)->llip = va_arg((VA_LIST), long long int*);\
		else\
			(A)->ip = va_arg((VA_LIST), int*);\
		break;\
	default:\
		(SCAN) = 0;\
	}\
}while(0)

// *bufferPtr is at least PRINT_BUFFER_SIZE
static int outputPrintfArg(const struct PrintfArg *pa, char **bufferPtr){
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
		if(pa->lCount >= 2){
			panic("lld not implemented");
		}
		else{
			bufferLength = signedToString(buffer, pa->i, (int)pa->base);
		}
		break;
	case 'b':
	case 'o':
	case 'u':
	case 'x':
		if(pa->lCount >= 2){
			panic("llu not implemented");
		}
		else{
			bufferLength = unsignedToString(buffer, pa->u, (int)pa->base);
		}
		break;
	case 'c':
	case NOT_FORMAT_SPECIFIER:
		buffer[0] = pa->c;
		bufferLength = 1;
		break;
	default:
		return -1;
	}
	return bufferLength;
}

static int skipSpace(const char *buffer){
	int readCount = 0;
	while(isspace(buffer[readCount])){
		readCount++;
	}
	return readCount;
}

static int inputScanfArg(const struct PrintfArg *pa, const char *buffer){
	int readCount = 0;
	// not skip space
	if(pa->format != 'c' && pa->format != '%' && pa->format != NOT_FORMAT_SPECIFIER){
		readCount += skipSpace(buffer + readCount);
	}
	if(buffer[readCount] == '\0'){
		return -1;
	}
	switch(pa->format){
	case '%':
		if(buffer[readCount] == '%')
			readCount++;
		else
			readCount = -1;
		break;
	case 's':
		{
			int i = 0;
			while(isspace(buffer[readCount]) == 0 && buffer[readCount] != '\0'){
				pa->cp[i] = buffer[readCount];
				i++;
				readCount++;
			}
		}
		break;
	case 'c':
		pa->cp[0] = buffer[readCount];
		readCount++;
		break;
	case 'd':
	case 'b':
	case 'o':
	case 'u':
	case 'x':
		if(pa->lCount >= 2){
			panic("lld not implemented");
		}
		else{
			int length = stringToSigned(buffer + readCount, pa->ip, (int)pa->base);
			if(length < 0){
				readCount = -1;}
			else
				readCount += length;
		}
		break;
	case NOT_FORMAT_SPECIFIER:
		if(isspace(pa->c))
			readCount += skipSpace(buffer + readCount);
		else if(buffer[readCount] == pa->c)
			readCount++;
		else
			readCount = -1;
		break;
	}
	return readCount;
}

int sprintf(char *str, const char *format, ...){
	int printCount = 0;
	va_list argList;
	va_start(argList, format);
	while(*format != '\0'){
		char numberBuffer[PRINT_BUFFER_SIZE];
		char *buffer = numberBuffer;
		struct PrintfArg pa;
		format = parsePrintfArg(format, &pa);
		SET_PRINTF_ARG(&pa, argList);
		int bufferLength = outputPrintfArg(&pa, &buffer);
		if(bufferLength < 0){
			printCount = -1;
			break;
		}
		strncpy(str + printCount, buffer, bufferLength);
		printCount += bufferLength;
	}
	if(printCount >= 0){
		str[printCount] = '\0';
	}
	va_end(argList);
	return printCount;
}

int printk(const char *format, ...){
	int printCount = 0;
	va_list argList;
	va_start(argList, format);
	while(*format != '\0'){
		char numberBuffer[PRINT_BUFFER_SIZE];
		char *buffer = numberBuffer;
		struct PrintfArg pa;
		format = parsePrintfArg(format, &pa);
		SET_PRINTF_ARG(&pa, argList);
		int bufferLength = outputPrintfArg(&pa, &buffer);
		if(bufferLength < 0){
			printCount = -1;
			break;
		}
		printString(buffer, bufferLength);
		printCount += bufferLength;
	}
	va_end(argList);
	return printCount;
}

int sscanf(const char *str, const char *format, ...){
	int scanCount = 0;
	va_list argList;
	va_start(argList, format);
	while(*format != '\0'){
		struct PrintfArg pa;
		format = parsePrintfArg(format, &pa);
		int isScanned;
		SET_SCANF_ARG(&pa, argList, isScanned);
		int scanLength = inputScanfArg(&pa, str);
		if(scanLength < 0){
			break;
		}
		str += scanLength;
		scanCount += (isScanned? 1: 0);
	}
	va_end(argList);
	return scanCount;
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

#ifndef NDEBUG
void testSscanf(void);
void testSscanf(void){
	int a, b, c;
	c = sscanf("+15-f", "%u%x",&a, &b);
	assert(c == 2 && a == 15 && b == -15);

	char s[]=" 345  a567a789";
	c = sscanf(s,"%d %da", &a, &b);
	assert(c == 1 && a == 345);

	c = sscanf(s,"%d a%da", &a, &b);
	assert(c == 2 && a == 345 && b == 567);

	c = sscanf(s,"%da a%d", &a, &b);
	assert(c == 1 && a == 345);

	char s2[]="1 % 2";
	c = sscanf(s2, "%d %%%d", &a, &b);
	assert(c == 2 && a == 1 && b == 2);

	char s3[] = "x y %z";
	char i, j, k, l;
	c = sscanf(s3,"%c %c%c%% %c", &i, &j, &k, &l);
	assert(c == 4 && i == 'x' && j == 'y' && k == ' ' && l =='z');
}
#endif
