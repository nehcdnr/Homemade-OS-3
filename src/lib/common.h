#include<std.h>

// non-standard NULL
//#define UINTPTR_NULL ((uintptr_t)NULL)
#define UINTPTR_NULL ((uintptr_t)NULL)

// memory.h
void *memset(void *ptr, unsigned char value, size_t size);
volatile void *memset_volatile(volatile void *ptr, unsigned char value, size_t size);
// MEMSET0 is not applicable to array
#define MEMSET0(P) memset((P), 0, sizeof(*(P)))
void *memcpy(void *dst, const void *src, size_t size);
volatile void *memcpy_volatile(volatile void *dst, volatile const void *src, size_t size);

// string.h
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strncpy(char *dst, const char* src, size_t n);

// ctype.h
int tolower(int c);
int toupper(int c);
int isspace(int c);

// assert.h
#define _TO_STRING(A) #A
#define TO_STRING(A) _TO_STRING(A)
#define _STRCAT(A,B) A##B
#define STRCAT(A,B) _STRCAT(A,B)

// eclipse parser does not recognize
#ifndef __BASE_FILE__
#define __BASE_FILE__ ""
#endif

#define EXPECT(CONDITION) do{if(!(CONDITION))break
#define ON_ERROR }while(0)

#define static_assert(A) enum{STRCAT(_ASSERT_,__COUNTER__)=1/(A)}

// other
#define DIV_CEIL(A, B) (((A)+(B)-1)/(B))
#define FLOOR(A,B) (((A)/(B))*(B))
#define CEIL(A,B) (FLOOR((A)+((B)-1),B))

#define LOW64(V) ((V) & 0xffffffff)
#define HIGH64(V) ((((uint64_t)(V)) >> 32) & 0xffffffff)
#define COMBINE64(H, L) ((uint64_t)(L) + ((uint64_t)(H) << 32))

uint16_t changeEndian16(uint16_t v);
uint32_t changeEndian32(uint32_t v);
uint64_t changeEndian64(uint64_t v);

int isStringEqual(const char *s1, uintptr_t len1, const char *s2, uintptr_t len2);
// '*' for 0 or more characters
int matchWildcardString(const char *str, uintptr_t sLen, const char *pat, uintptr_t pLen);
uintptr_t indexOf(const char *s, uintptr_t i, uintptr_t len, char c);
uintptr_t indexOfNot(const char *s, uintptr_t i, uintptr_t len, char c);
// return type of ternary operator is the 1st value
#define MAX(A,B) ((A)>(B)?(A):(B))
#define MIN(A,B) ((A)<(B)?(A):(B))

#define LENGTH_OF(A) (sizeof(A)/sizeof((A)[0]))
#define MEMBER_OFFSET(T, M) ((size_t)&(((T*)0)->M))
//#define ASSIGN_TO_CONST(A, B) ((*(typeof(A)*)&(A)) = (B))

#define IS_IN_DQUEUE(E) ((E)->prev != NULL)

#define REMOVE_FROM_DQUEUE(E) do{\
	*((E)->prev) = (E)->next;\
	if((E)->next != NULL){\
		(E)->next->prev = (E)->prev; \
		(E)->next = NULL;\
	}\
	(E)->prev = NULL;\
}while(0)

#define ADD_TO_DQUEUE(E, P) do{\
	(E)->prev = (P);\
	(E)->next = *(P);\
	if((E)->next != NULL){\
		(E)->next->prev = &((E)->next);\
	}\
	*((E)->prev) = (E);\
}while(0)
