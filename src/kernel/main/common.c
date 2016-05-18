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

static_assert(sizeof(unsigned char) == 1);

#define MEMSET(D, I, V, SIZE) \
size_t (I);\
for((I) = 0; (I) < (SIZE); (I)++){\
	((unsigned char*)(D))[(I)] = (V);\
}\
return (D)

void *memset(void *ptr, unsigned char value, size_t size){
	MEMSET(ptr, i, value, size);
}

volatile void *memset_volatile(volatile void *ptr, unsigned char value, size_t size){
	MEMSET(ptr, i, value, size);
}

void *memcpy(void *dst, const void *src, size_t size){
	MEMSET(dst, i, ((unsigned char*)src)[i], size);
}

volatile void *memcpy_volatile(volatile void *dst, volatile const void *src, size_t size){
	MEMSET(dst, i, ((volatile unsigned char*)src)[i], size);
}
#undef MEMSET

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

#define UNSIGNED_TO_STRING(NAME, T) \
static int NAME##ToString(char *str, T number, unsigned base){\
	int i = 0;\
	T n2 = number;\
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

UNSIGNED_TO_STRING(uint, unsigned)
UNSIGNED_TO_STRING(ull, unsigned long long)

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

static int stringToUnsigned(const char *buffer, int bufferLength, unsigned int *u, int base){
	int i;
	*u = 0;
	for(i = 0; i < bufferLength; i++){
		const char b = buffer[i];
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
	return i == 0? -1: i;
}

static int stringToSigned(const char *buffer, int bufferLength, int *result, int base){
	// sign
	int i1 = 0;
	int s = 1;
	if(bufferLength < 1)
		return 0;
	if(*buffer == '+'){
		s = 1;
		i1 = 1;
	}
	else if(*buffer == '-'){
		s = -1;
		i1 = 1;
	}
	else{
		i1 = 0;
	}
	// digits
	unsigned u;
	int i2 = stringToUnsigned(buffer + i1, bufferLength - i1, &u, base);
	if(i2 < 0)
		return -1;
	*result = ((int)u) * s;
	return i1 + i2;
}

#define CHANGE_ENDIAN(V, L) \
(((typeof(V))changeEndian##L((V) & ((((typeof(V))1) << (L)) - 1))) << (L)) + \
(((typeof(V))changeEndian##L((V) >> (L))) & ((((typeof(V))1) << (L)) - 1))

#define changeEndian8(V) (V)

uint16_t changeEndian16(uint16_t v){
	return CHANGE_ENDIAN(v, 8);
}

uint32_t changeEndian32(uint32_t v){
	return CHANGE_ENDIAN(v, 16);
}

uint64_t changeEndian64(uint64_t v){
	return CHANGE_ENDIAN(v, 32);
}

#undef changeEndian8
#undef CHANGE_ENDIAN

int isStringEqual(const char *s1, uintptr_t len1, const char *s2, uintptr_t len2){
	if(len1 != len2){
		return 0;
	}
	return strncmp(s1, s2, len1) == 0;
}

int matchWildcardString(const char *str, uintptr_t sLen, const char *pat, uintptr_t pLen){
	// match non wildcard at tail
	const char wildcard = '*';
	while(pLen > 0 && sLen > 0){
		if(pat[pLen - 1] == wildcard)
			break;
		if(pat[pLen - 1] != str[sLen - 1])
			return 0;
		pLen--;
		sLen--;
	}
	if(pLen == 0){
		return sLen == 0;
	}
	// match pattern as a sequence of x...*...
	uintptr_t s0 = 0, p0 = 0;
	while(p0 < pLen){
		uintptr_t p1;
		for(p1 = p0; p1 < pLen && pat[p1] != wildcard; p1++);
		while(1){
			if(s0 + p1 - p0 > sLen)
				return 0;
			if(isStringEqual(str + s0, p1 - p0, pat + p0, p1 - p0)){
				s0 += p1 - p0;
				break;
			}
			s0++;
		}
		for(p0 = p1; p0 < pLen && pat[p0] == wildcard; p0++);
	}
	return 1;
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

typedef struct PrintfArg{
	int printCount;
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
}PrintfArg;

// support format: csdbouxn
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
	case 'n':
	case 'u':
	case 'd':
		pa->base = 10;
		break;
	case 'b':
		pa->base = 2;
		break;
	case 'o':
		pa->base = 8;
		break;
	case 'x':
		pa->base = 16;
		break;
	}
	pa->format = *format;
	return format + (*format != '\0'? 1: 0);
}

static int setScanfArg(struct PrintfArg *pa, va_list *vaList){
	int scanCount = 1;
	switch(pa->format){
	case 'c':
		pa->cp = va_arg(*vaList, char*);
		break;\
	case 's':\
		pa->cp = va_arg(*vaList, char*);
		break;

	case 'd':
	case 'b':
	case 'o':
	case 'u':
	case 'x':
		if(pa->lCount >= 2)
			pa->llip = va_arg(*vaList, long long int*);
		else
			pa->ip = va_arg(*vaList, int*);
		break;
	case 'n':
		pa->ip = va_arg(*vaList, int*);
		scanCount = 0;
		break;
	default:
		scanCount = 0;
	}
	return scanCount;
}

static int skipSpace(const char *buffer, int bufferLength){
	int readCount;
	for(readCount = 0; readCount < bufferLength; readCount++){
		if(isspace(buffer[readCount]) == 0)
			break;
	}
	return readCount;
}

static int outputScanfArg(const struct PrintfArg *pa, const char *buffer, int readCount, int bufferLength){
	// not skip space
	if(pa->format != 'c' && pa->format != '%' && pa->format != 'n' && pa->format != NOT_FORMAT_SPECIFIER){
		readCount += skipSpace(buffer + readCount, bufferLength - readCount);
	}
	if(readCount == bufferLength && pa->format != 'n'){
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
			while(readCount < bufferLength){
				if(isspace(buffer[readCount]))
					break;
				pa->cp[i] = buffer[readCount];
				i++;
				readCount++;
			}
			pa->cp[i] = '\0';
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
			int length = stringToSigned(buffer + readCount, bufferLength - readCount, pa->ip, (int)pa->base);
			if(length < 0)
				readCount = -1;
			else{
				assert(readCount + length <= bufferLength);
				readCount += length;
			}
		}
		break;
	case 'n':
		pa->ip[0] = readCount;
		break;
	case NOT_FORMAT_SPECIFIER:
		if(isspace(pa->c))
			readCount += skipSpace(buffer + readCount, bufferLength - readCount);
		else if(buffer[readCount] == pa->c)
			readCount++;
		else
			readCount = -1;
		break;
	}
	return readCount;
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

#define PRINTF_(NAME, SIGNED, B) \
static int printf_##NAME (char **buffer, const PrintfArg *arg, va_list *argList){\
	return printf_##SIGNED((B), buffer, arg, argList);\
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
/*
static int printf_percent(char **buffer, __attribute__((__unused__)) const PrintfArg *arg, __attribute__((__unused__)) va_list *argList){
	(*buffer)[0] = '%';
	return 1;
}
*/
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

typedef int PrintfHandler(char **buffer, const PrintfArg *arg, va_list *argList);

#define PRINTF_HANDLER(NAME) {TO_STRING(NAME), printf_##NAME}

static const struct{
	const char *specifier;
	PrintfHandler *handler;
}printfHandler[] = {
	//{"%", printf_percent},
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

static int parsePrintfArg2(const char **format, struct PrintfBuffer *bufPtr, va_list *argList, int printCount){
	bufPtr->buffer = bufPtr->_fixedBuffer;
	if(**format != '%'){
		bufPtr->buffer[0] = **format;
		(*format)++;
		return 1;
	}
	const char *f = (*format) + 1;
	PrintfArg pa;
	pa.printCount = printCount;
	pa.width = 0;
	pa.pad0 = 0;
	pa.lCount = 0;
	pa.base = 0;
	while(*f == '0'){
		pa.pad0 = 1;
		f++;
	}
	while(*f >= '0' && *f <= '9'){
		pa.width = pa.width * 10 + (*f - '0');
		f++;
	}
	while(*f == 'l'){
		pa.lCount++;
		f++;
	}
	int r;
	unsigned i;
	for(i = 0; i < LENGTH_OF(printfHandler); i++){
		int specLength = strlen(printfHandler[i].specifier);
		if(strncmp(printfHandler[i].specifier, f, specLength) == 0){
			r = printfHandler[i].handler(&bufPtr->buffer, &pa, argList);
			f += specLength;
			break;
		}
	}
	if(i == LENGTH_OF(printfHandler)){
		r = 1;
		bufPtr->buffer[0] = *f;
		f++;
	}
	*format = f;
	return r;
}

static int vsnprintf(char *str, size_t len, const char *format, va_list *argList){
	int slen = len;
	int printCount = 0;
	while(*format != '\0' && printCount < slen){
		struct PrintfBuffer bufferPtr;
		int bufferLength = parsePrintfArg2(&format, &bufferPtr, argList, printCount);
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
		int bufferLength = parsePrintfArg2(&format, &bufferPtr, &argList, printCount);
		if(bufferLength < 0){
			printCount = -1;
			break;
		}
		printString(bufferPtr.buffer, bufferLength);
		printCount += bufferLength;
	}
	va_end(argList);
	return printCount;
}

static int vsnscanf(const char *str, size_t len, const char *format, va_list *argList){
	int scanCount = 0;
	int scanLength = 0;
	while(*format != '\0'){
		struct PrintfArg pa;
		format = parsePrintfArg(format, &pa);
		int isScanned = setScanfArg(&pa, argList);
		int newScanLength = outputScanfArg(&pa, str, scanLength, len);
		if(newScanLength < 0){
			break;
		}
		scanLength = newScanLength;
		scanCount += (isScanned? 1: 0);
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

	char s4[20];
	memset(s4, 'a', 20 * sizeof(s4[0]));
	c = sscanf(" aaa bbb ", "%s", s4);
	assert(c == 1 && strlen(s4) == 3);

	c = sscanf("123 456", "%d%n456", &a, &b);
	assert(c == 1 && a == 123 && b == 3);

	c = sscanf("123  456", "%n123 %n", &a, &b);
	assert(c == 0 && a == 0 && b == 5);

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
