#include<std.h>

// memory.h
void *memset(void *ptr, unsigned char value, size_t size);
#define MEMSET0(P) memset((P), 0, sizeof(*(P)))
void *memcpy(void *dst, const void *src, size_t size);

// string.h
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strncpy(char *dst, const char* src, size_t n);

// assert.h
void printAndHalt(const char *condition, const char *file, int line);
#define _TO_STRING(A) #A
#define TO_STRING(A) _TO_STRING(A)
#define _STRCAT(A,B) A##B
#define STRCAT(A,B) _STRCAT(A,B)

// eclipse error
#ifndef __BASE_FILE__
#define __BASE_FILE__ ""
#endif

#define panic(A) printAndHalt((A), __BASE_FILE__, __LINE__)
#ifndef NDEBUG
	#define assert(A) do{if(!(A)){panic(TO_STRING(A));}}while(0)
#else
	#define assert(A) do{}while(0)
#endif

#define static_assert(A) enum{STRCAT(_ASSERT_,__COUNTER__)=1/(A)}

// other
#define DIV_CEIL(A, B) (((A)+(B)-1)/(B))
int printk(const char *format, ...);
#define LENGTH_OF(A) (sizeof(A)/sizeof((A)[0]))
#define ASSIGN_TO_CONST(A, B) ((*(typeof(A)*)&(A)) = (B))

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
