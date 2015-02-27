#include "std.h"

// memory.h
void *memset(void *ptr, unsigned char value, size_t size);
void *memcpy(void *dst, const void *src, size_t size);

// string.h
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int n);

// assert.h
void printAndHalt(const char *condition, const char *file, int line);
#define TO_STRING(A) #A
#define _STRCAT(A,B) A##B
#define STRCAT(A,B) _STRCAT(A,B)
#define panic(A) printAndHalt((A), __FILE__, __LINE__)
#ifndef NDEBUG
	#define assert(A) do{if(!(A)){panic(TO_STRING(A));}}while(0)
#else
	#define assert(A) do{}while(0)
#endif

#define static_assert(A) enum{STRCAT(_ASSERT_,__COUNTER__)=1/(A)}

// other
#define DIV_CEIL(A ,B) (((A)+(B)-1)/(B))
int kprintf(const char *format, ...);
