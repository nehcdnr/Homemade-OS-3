#include"common.h"

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
