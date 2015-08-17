#include<std.h>

// non-standard NULL
#define UINTPTR_NULL ((uintptr_t)NULL)

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

// eclipse parser does not recognize
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

#define EXPECT(CONDITION) do{if(!(CONDITION))break
#define ON_ERROR }while(0)

// other
#define DIV_CEIL(A, B) (((A)+(B)-1)/(B))
#define FLOOR(A,B) (((A)/(B))*(B))
#define CEIL(A,B) (FLOOR((A)+((B)-1),B))

int printk(const char *format, ...);
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

#define FIND_DQUEUE_TAIL(P) do{\
	while((*(P)) != NULL){\
		(P) = &((*(P))->next);\
	}\
}while(0)

// append P1 to P2
#define APPEND_TO_DQUEUE(P1, P2) do{\
	assert((*(P2)) == NULL);\
	(*(P2)) = (*(P1));\
	if((*(P1)) != NULL){\
		assert((*(P1))->prev == (P1));\
		(*(P1))->prev = (P2);\
		(*(P1)) = NULL;\
	}\
}while(0)
