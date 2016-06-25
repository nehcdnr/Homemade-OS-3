#include"common.h"
#include"kernel.h"
#include"assembly/assembly.h"

#define UNSIGNED_TO_STRING(NAME, T) \
static int NAME##ToString(char *str, unsigned T number, unsigned base){\
	int i = 0;\
	unsigned T n2 = number;\
	do{\
		n2 = n2 / base;\
		i++;\
	}while(n2 != 0);\
	n2 = number;\
	int len = i;\
	for(i--; i >= 0; i--){\
		unsigned c = n2 % base;\
		str[i] = (c < 10? c + '0': c - 10 + 'a');\
		n2 /= base;\
	}\
	return len;\
}

UNSIGNED_TO_STRING(uint, int)
UNSIGNED_TO_STRING(ull, long long)

#undef UNSIGNED_TO_STRING

#define SIGNED_TO_STRING(NAME, T) \
static int NAME##ToString(char *str, T number, int base){\
	int printMinus = 0;\
	if(number < 0){\
		str[0] = '-';\
		printMinus = 1;\
		number = -number;\
	}\
	return printMinus + u##NAME##ToString(str + printMinus, (unsigned T)number, base);\
}

SIGNED_TO_STRING(int, int)
SIGNED_TO_STRING(ll, long long)

#undef SIGNED_TO_STRING

#define STRING_TO_UNSIGNED(NAME, T) \
static int stringTo##NAME(const char *buffer, int bufferLength, unsigned T *u, unsigned base){\
	int i;\
	*u = 0;\
	for(i = 0; i < bufferLength; i++){\
		const char b = buffer[i];\
		unsigned d = base;\
		if(b >= 'A' && b <= 'Z')\
			d = b - 'A' + 10;\
		if(b >= 'a' && b <= 'z')\
			d = b - 'a' + 10;\
		else if(b >= '0' && b <= '9')\
			d = b - '0';\
		if(d >= base)\
			break;\
		(*u) = (*u) * base + d;\
	}\
	return i == 0? -1: i;\
}

STRING_TO_UNSIGNED(UINT, int)
STRING_TO_UNSIGNED(ULL, long long)

#undef STRING_TO_UNSIGNED

#define STRING_TO_SIGNED(NAME, T) \
static int stringTo##NAME(const char *buffer, int bufferLength, T *result, unsigned base){\
	/*sign*/\
	int i1 = 0;\
	int s = 1;\
	if(bufferLength < 1)\
		return 0;\
	if(*buffer == '+'){\
		s = 1;\
		i1 = 1;\
	}\
	else if(*buffer == '-'){\
		s = -1;\
		i1 = 1;\
	}\
	else{\
		i1 = 0;\
	}\
	/*digits*/\
	unsigned T u;\
	int i2 = stringToU##NAME(buffer + i1, bufferLength - i1, &u, base);\
	if(i2 < 0)\
		return -1;\
	*result = ((T)u) * s;\
	return i1 + i2;\
}

STRING_TO_SIGNED(INT, int)
STRING_TO_SIGNED(LL, long long)

#undef STRING_TO_SIGNED

typedef struct PrintfArg{
	int printCount;
	int width;
	char pad0;
	char lCount;
	char base;
}PrintfArg;

// support format: %csdbouxnI

static const char *parsePrintfArg(const char *format, PrintfArg *pa, int printCount){
	pa->printCount = printCount;
	pa->width = 0;
	pa->pad0 = 0;
	pa->lCount = 0;
	pa->base = 0;
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
	return format;
}

#define PRINTF_NUMBER(INT_NAME, LL_NAME, SIGNED)\
static int printf_##SIGNED(int base, char **buffer, const PrintfArg *arg, va_list *argList){\
	if(arg->lCount >= 2){\
		SIGNED long long v = va_arg(*argList, SIGNED long long);\
		return LL_NAME##ToString(*buffer, v, base);\
	}\
	else{\
		SIGNED int v = va_arg(*argList, SIGNED int);\
		return INT_NAME##ToString(*buffer, v, base);\
	}\
}

PRINTF_NUMBER(int, ll, signed)
PRINTF_NUMBER(uint, ull, unsigned)

#undef PRINTF_NUMBER

#define PRINTF_(NAME, SIGNED, BASE) \
static int printf_##NAME (char **buffer, const PrintfArg *arg, va_list *argList){\
	return printf_##SIGNED((BASE), buffer, arg, argList);\
}

PRINTF_(b, unsigned, 2)
PRINTF_(d, signed, 10)
PRINTF_(u, unsigned, 10)
PRINTF_(o, unsigned, 8)
PRINTF_(x, unsigned, 16)

#undef PRINTF_

static int printf_s(char **buffer, __attribute__((__unused__)) const PrintfArg *arg, va_list *argList){
	*buffer = va_arg(*argList, char*);
	return strlen(*buffer);
}

static int printf_percent(char **buffer, __attribute__((__unused__)) const PrintfArg *arg, __attribute__((__unused__)) va_list *argList){
	(*buffer)[0] = '%';
	return 1;
}

static int printf_c(char **buffer, __attribute__((__unused__)) const PrintfArg *arg, va_list *argList){
	// char promoted to int
	int v = va_arg(*argList, int);
	(*buffer)[0] = v;
	return 1;
}

static int printf_I(char **buffer, __attribute__((__unused__)) const PrintfArg *arg, va_list *argList){
	unsigned int v = va_arg(*argList, unsigned int);
	char *b = *buffer;
	unsigned i;
	for(i = 0; i < sizeof(v) * 8; i += 8){
		if(i != 0){
			*b = '.';
			b++;
		}
		b += uintToString(b, ((v >> i) & 0xff), 10);
	}
	return b - *buffer;
}

static int printf_n(__attribute__((__unused__)) char **buffer, const PrintfArg *arg, va_list *argList){
	int *p = va_arg(*argList, int*);
	*p = arg->printCount;
	return 0;
}

// return buffer length
// if the argument is a string, set *buffer to it
typedef int PrintfHandler(char **buffer, const PrintfArg *arg, va_list *argList);

#define PRINTF_HANDLER(NAME) {TO_STRING(NAME), printf_##NAME}

static const struct{
	const char *specifier;
	PrintfHandler *handler;
}printfHandler[] = {
	{"%", printf_percent},
	PRINTF_HANDLER(b),
	PRINTF_HANDLER(o),
	PRINTF_HANDLER(d),
	PRINTF_HANDLER(u),
	PRINTF_HANDLER(x),
	PRINTF_HANDLER(n),
	PRINTF_HANDLER(s),
	PRINTF_HANDLER(c),
	PRINTF_HANDLER(I)
};

#undef PRINTF_HANDLER

// the longest format is %llb
#define PRINT_BUFFER_SIZE (sizeof(long long) * 8 + 4)

struct PrintfBuffer{
	char *buffer;
	char _fixedBuffer[PRINT_BUFFER_SIZE];
};

static int vsnprintf_single(const char **format, struct PrintfBuffer *bufPtr, int printCount, va_list *argList){
	bufPtr->buffer = bufPtr->_fixedBuffer;
	const char *f = *format;
	int r;
	if(*f != '%'){
		bufPtr->buffer[0] = *f;
		f++;
		r = 1;
	}
	else/* if(*f == '%')*/{
		PrintfArg pa;
		f = parsePrintfArg(f + 1, &pa, printCount);
		r = -1;
		unsigned i;
		for(i = 0; i < LENGTH_OF(printfHandler); i++){
			int specLength = strlen(printfHandler[i].specifier);
			if(strncmp(printfHandler[i].specifier, f, specLength) == 0){
				r = printfHandler[i].handler(&bufPtr->buffer, &pa, argList);
				f += specLength;
				break;
			}
		}
	}
	*format = f;
	return r;
}

static int vsnprintf(char *str, size_t len, const char *format, va_list *argList){
	int slen = len;
	int printCount = 0;
	while(*format != '\0' && printCount < slen){
		struct PrintfBuffer bufferPtr;
		int bufferLength = vsnprintf_single(&format, &bufferPtr, printCount, argList);
		if(bufferLength < 0){
			printCount = -1;
			break;
		}
		if(bufferLength > slen - printCount){
			bufferLength = slen - printCount;
		}
		memcpy(str + printCount, bufferPtr.buffer, bufferLength * sizeof(bufferPtr.buffer[0]));
		printCount += bufferLength;
	}
	if(printCount >= 0 && printCount < slen){
		str[printCount] = '\0';
	}
	return printCount;
}

int snprintf(char *str, size_t len, const char *format, ...){
	va_list argList;
	va_start(argList, format);
	int printCount = vsnprintf(str, len, format, &argList);
	va_end(argList);
	return printCount;
}

int printk(const char *format, ...){
	va_list argList;
	va_start(argList, format);
	int printCount = 0;
	while(*format != '\0'){
		struct PrintfBuffer bufferPtr;
		int bufferLength = vsnprintf_single(&format, &bufferPtr, printCount, &argList);
		if(bufferLength < 0){
			printCount = -1;
			break;
		}
		printkString(bufferPtr.buffer, bufferLength);
		printCount += bufferLength;
	}
	va_end(argList);
	return printCount;
}

#define SCANF_NUMBER(INT_NAME, LL_NAME, SIGNED) \
static int scanf_##SIGNED(int base, const char *buffer, int bufferLength, PrintfArg *arg, va_list *argList){\
	if(arg->lCount >= 2){\
		SIGNED long long *v = va_arg(*argList, SIGNED long long*);\
		return stringTo##LL_NAME(buffer, bufferLength, v, base);\
	}\
	else{\
		SIGNED int *v = va_arg(*argList, SIGNED int*);\
		return stringTo##INT_NAME(buffer, bufferLength, v, base);\
	}\
}

SCANF_NUMBER(INT, LL, signed)
//SCANF_NUMBER(UINT, ULL, unsigned)

#undef SCANF_NUMBER

#define SCANF_(NAME, SIGNED, BASE) \
static int scanf_##NAME(const char *buffer, int bufferLength, PrintfArg *arg, va_list *argList){\
	return scanf_##SIGNED((BASE), buffer, bufferLength, arg, argList);\
}

SCANF_(b, signed, 2)
SCANF_(o, signed, 8)
SCANF_(u, signed, 10) // d and u are equivalent in scanf
SCANF_(d, signed, 10)
SCANF_(x, signed, 16)

#undef SCANF_

static int scanf_n(__attribute__((__unused__)) const char *buffer, __attribute__((__unused__)) int bufferLength, PrintfArg *arg, va_list *argList){
	int *p = va_arg(*argList, int*);
	*p = arg->printCount;
	return 0;
}

static int scanf_s(const char *buffer, int bufferLength, __attribute__((__unused__)) PrintfArg *arg, va_list *argList){
	char *a = va_arg(*argList, char*);
	int i = 0;
	while(i < bufferLength){
		if(isspace(buffer[i]))
			break;
		a[i] = buffer[i];
		i++;
	}
	a[i] = '\0';
	return (i > 0? i: -1);
}

static int scanf_percent(
	const char *buffer, int bufferLength,
	__attribute__((__unused__)) PrintfArg *arg, __attribute__((__unused__)) va_list *argList
){
	if(bufferLength == 0 || buffer[0] != '%'){
		return -1;
	}
	return 1;
}

static int scanf_c(const char *buffer, int bufferLength, __attribute__((__unused__)) PrintfArg *arg, va_list *argList){
	if(bufferLength == 0){
		return -1;
	}
	char *p = va_arg(*argList, char*);
	*p = buffer[0];
	return 1;
}

static int scanf_I(const char *buffer, int bufferLength, __attribute__((__unused__)) PrintfArg *arg, va_list *argList){
	int bi = 0;
	int i;
	unsigned *p = va_arg(*argList, unsigned*);
	*p = 0;
	for(i = 0; i < 32; i += 8){
		if(i != 0){
			if(buffer[bi] != '.'){
				return -1;
			}
			bi++;
		}
		unsigned u;
		int bi2 = stringToUINT(buffer + bi, bufferLength - bi, &u, 10);
		if(bi2 < 0 || u > 0xff){
			return -1;
		}
		bi += bi2;
		(*p) |= (u << i);
	}
	return bi;
}

// return number of bytes read from buffer
typedef int ScanfHandler(const char *buffer, int bufferLength, PrintfArg *arg, va_list *argList);

#define SCANF_HANDLER(NAME, ADD_SCAN_COUNT, SKIP_SPACE) {TO_STRING(NAME), (ADD_SCAN_COUNT), (SKIP_SPACE), scanf_##NAME}
struct{
	const char *specifier;
	int addScanCount;
	int skipSpace;
	ScanfHandler *handler;
}scanfHandler[] = {
	{"%", 0, 0, scanf_percent},
	SCANF_HANDLER(b, 1, 1),
	SCANF_HANDLER(o, 1, 1),
	SCANF_HANDLER(d, 1, 1),
	SCANF_HANDLER(u, 1, 1),
	SCANF_HANDLER(x, 1, 1),
	SCANF_HANDLER(n, 0, 0),
	SCANF_HANDLER(s, 1, 1),
	SCANF_HANDLER(c, 1, 0),
	SCANF_HANDLER(I, 1, 1)
};
#undef SCANF_HANDLER

static int skipSpace(const char *buffer, int bufferLength){
	int readCount;
	for(readCount = 0; readCount < bufferLength; readCount++){
		if(isspace(buffer[readCount]) == 0)
			break;
	}
	return readCount;
}

static int vsnscanf_single(const char *buffer, int *bufferIndex, size_t bufferLength, const char **format, va_list *argList){
	const char *f = *format;
	int bi = *bufferIndex;
	int r = 0;
	if(f[0] == ' '){
		bi += skipSpace(buffer + bi, bufferLength - bi);
		f++;
	}
	else if(f[0] != '%'){
		if(buffer[bi] != f[0])
			return -1;
		f++;
		bi++;
	}
	else/* if(f[0] == '%')*/{
		PrintfArg pa;
		f = parsePrintfArg(f + 1, &pa, bi);
		r = -1;
		unsigned i;
		for(i = 0; i < LENGTH_OF(scanfHandler); i++){
			int specLength = strlen(scanfHandler[i].specifier);
			if(strncmp(scanfHandler[i].specifier, f, specLength) == 0){
				if(scanfHandler[i].skipSpace){
					bi += skipSpace(buffer + bi, bufferLength - bi);
				}
				int scanLength = scanfHandler[i].handler(buffer + bi, bufferLength - bi, &pa, argList);
				if(scanLength < 0){
					r = -1;
				}
				else{
					bi += scanLength;
					r = scanfHandler[i].addScanCount;
					f += specLength;
				}
				break;
			}
		}
	}
	*format = f;
	*bufferIndex = bi;
	return r;
}

static int vsnscanf(const char *str, size_t len, const char *format, va_list *argList){
	int scanCount = 0;
	int scanLength = 0;
	while(*format != '\0'){
		int newScanCount = vsnscanf_single(str, &scanLength, len, &format, argList);
		if(newScanCount < 0){
			break;
		}
		scanCount += newScanCount;
	}
	return scanCount;
}

int snscanf(const char *str, size_t len, const char *format, ...){
	va_list argList;
	va_start(argList, format);
	int scanCount = vsnscanf(str, len, format, &argList);
	va_end(argList);
	return scanCount;
}

int sscanf(const char *str, const char *format, ...){
	va_list argList;
	va_start(argList, format);
	int scanCount = vsnscanf(str, strlen(str), format, &argList);
	va_end(argList);
	return scanCount;
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
	c = sscanf("+15-f", "%u%x", &a, &b);
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

	char s4[20];
	memset(s4, 'a', 20 * sizeof(s4[0]));
	c = sscanf(" aaa bbb ", "%s", s4);
	assert(c == 1 && strlen(s4) == 3);

	c = sscanf("123 456", "%d%n456", &a, &b);
	assert(c == 1 && a == 123 && b == 3);

	c = sscanf("123  456", "%n123 %n", &a, &b);
	assert(c == 0 && a == 0 && b == 5);

	long long d, e;
	c = sscanf(" -10000000000 ffffffffffffffff ", "%lld %llx", &d, &e);
	assert(c == 2 && d == -100000 * (long long)100000 && e == (long long)-1);

	c = sscanf(" 192.168.100.255", "%I", &a);
	assert(c == 1 && (unsigned)a == 0xff64a8c0);

	printk("test sscanf ok\n");
}

void testPrintf(void);
void testPrintf(void){
	int a, b, c;
	char s1[20];
	const int len1 = 20;
	c = snprintf(s1, len1, "%d %d ", -99, 99);
	assert(isStringEqual(s1, c, "-99 99 ", 7));

	c = snprintf(s1, len1, "%b %u %o %x", 10, 10, 10, 10);
	assert(isStringEqual(s1, c, "1010 10 12 a", 12));

	c = snprintf(s1, len1, "%c%s%c", 'a', "bc", 'd');
	assert(isStringEqual(s1, c, "abcd", 4));

	c = snprintf(s1, len1, "123%n%d%n789", &a, 456, &b);
	assert(isStringEqual(s1, c, "123456789", 9) && a == 3 && b == 6);

	c = snprintf(s1, 3, "%%%%  ");
	assert(isStringEqual(s1, c, "%% ", 3));

	c = snprintf(s1, len1, "%I", 0xff64a8c0);
	assert(isStringEqual(s1, c, "192.168.100.255", 15));

	c = snprintf(s1, len1, "%lld", -100000 * (long long)100000);
	assert(isStringEqual(s1, c, "-10000000000", 12));

	c = snprintf(s1, len1, "%llx", COMBINE64(0xffffffff, 0xffffffff));
	assert(isStringEqual(s1, c, "ffffffffffffffff", 16));

	printk("test snprintf ok\n");
}

#define MATCH_WILDCARD(S, P) matchWildcardString((S), strlen(S), (P), strlen(P))
void testWildcard(void);
void testWildcard(void){
	int r;
	r = MATCH_WILDCARD("abc", "");
	assert(r == 0);
	r = MATCH_WILDCARD("", "");
	assert(r == 1);
	r = MATCH_WILDCARD("abc", "abc");
	assert(r == 1);
	r = MATCH_WILDCARD("abc", "**a**");
	assert(r == 1);
	r = MATCH_WILDCARD("abc", "a*a");
	assert(r == 0);
	r = MATCH_WILDCARD("abc", "*c");
	assert(r == 1);
	r = MATCH_WILDCARD("abc", "*");
	assert(r == 1);
	r = MATCH_WILDCARD("abc", "a**b**c**");
	assert(r == 1);
	r = MATCH_WILDCARD("abc", "bc");
	assert(r == 0);
	printk("test wildcard ok\n");
}
void testEndian(void);
void testEndian(void){
	assert(changeEndian16(0x1122) == 0x2211);
	assert(changeEndian32(0x11223344) == 0x44332211);
	assert(changeEndian64(COMBINE64(0xaabbccdd, 0x11223344)) == COMBINE64(0x44332211, 0xddccbbaa));
}

#endif
