#include"std.h"

// see console.c
int printkString(const char *s, size_t length);

int snprintf(char *str, size_t len, const char *format, ...);
int printk(const char *format, ...);
int snscanf(const char *str, size_t len, const char *format, ...);
int sscanf(const char *str, const char *format, ...);

// assert.h
void printAndHalt(const char *condition, const char *file, int line);
#define panic(A) printAndHalt((A), __BASE_FILE__, __LINE__)
#ifndef NDEBUG
	#define assert(A) do{if(!(A)){panic(TO_STRING(A));}}while(0)
#else
	#define assert(A) do{}while(0)
#endif
