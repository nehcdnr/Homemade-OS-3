
// memory.h
void *memset(void *ptr, unsigned char value, int size);
void* memcpy(void* dst, const void*src, int size);

// string.h
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int n);

// stdio.h
int printf(const char *format, ...);

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

// stddef.h
#define NULL ((void*)0)

// stdarg.h
typedef __builtin_va_list va_list;
#define va_start(VA_LIST, PRECEDING_PARAMETER) __builtin_va_start(VA_LIST, PRECEDING_PARAMETER)
#define va_arg(VA_LIST, PARAMETER_TYPE) __builtin_va_arg(VA_LIST, PARAMETER_TYPE)
#define va_end(VA_LIST) __builtin_va_end(VA_LIST)

