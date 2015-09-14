#ifndef STD_H_INCLUDED
#define STD_H_INCLUDED

// stdint.h
typedef char int8_t;
typedef unsigned char uint8_t;
// typedef signed short int16_t;
typedef unsigned short uint16_t;
// typedef signed int int32_t;
typedef unsigned int uint32_t;
// typedef signed long long int64_t;
typedef unsigned long long uint64_t;
typedef uint32_t uintptr_t;

// stddef.h
typedef unsigned int size_t;
#define NULL ((void*)0)

// stdarg.h
typedef __builtin_va_list va_list;
#define va_start(VA_LIST, PRECEDING_PARAMETER) __builtin_va_start(VA_LIST, PRECEDING_PARAMETER)
#define va_arg(VA_LIST, PARAMETER_TYPE) __builtin_va_arg(VA_LIST, PARAMETER_TYPE)
#define va_end(VA_LIST) __builtin_va_end(VA_LIST)

#endif
