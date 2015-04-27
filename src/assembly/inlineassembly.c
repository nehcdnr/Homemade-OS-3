
#include"assembly.h"

uint16_t getCS(void){
	uint16_t c;
	__asm__(
	"mov %%cs, %0\n"
	:"=a"(c)
	:
	);
	return c;
}

uint16_t getDS(void){
	uint16_t d;
	__asm__(
	"mov %%ds, %0\n"
	:"=a"(d)
	:
	);
	return d;
}

uint32_t getCR0(void){
	uint32_t value = 0;
	__asm__(
	"mov %%cr0, %0\n"
	:"=a"(value)
	:
	);
	return value;
}

void setCR0(uint32_t value){
	__asm__(
	"mov %0, %%cr0\n"
	:
	:"a"(value)
	);
}

#define IN(TYPE, FUNC) \
TYPE FUNC(uint16_t port){\
	TYPE value;\
	__asm__(\
	"in %1, %0\n"\
	:"=a"(value)\
	:"d"(port)\
	);\
	return value;\
}

IN(uint8_t, in8);
IN(uint16_t, in16);
IN(uint32_t, in32);
#undef IN

#define OUT(TYPE, FUNC) \
void FUNC(uint16_t port, TYPE value){\
	__asm__(\
	"out %1, %0\n"\
	:\
	:"d"(port), "a"(value)\
	);\
}

OUT(uint8_t, out8);
OUT(uint16_t, out16);
OUT(uint32_t, out32);
#undef OUT

static void cpuid(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx){
	__asm__(
	"cpuid\n"
	:"=a"(*eax),"=b"(*ebx),"=c"(*ecx),"=d"(*edx)
	:"a"(*eax),"c"(*ecx)
	);
}

int cpuid_hasAPIC(void){
	uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
	cpuid(&eax, &ebx, &ecx, &edx);
	return (edx >> 9) & 1;
}

int cpuid_getInitialAPICID(){
	uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
	cpuid(&eax, &ebx, &ecx, &edx);
	return (ebx >> 24) & 0xff;
}

void rdmsr(enum MSR ecx, uint32_t *edx,uint32_t *eax){
	__asm__(
	"rdmsr\n"
	:"=d"(*edx),"=a"(*eax)
	:"c"(ecx)
	);
}

void wrmsr(enum MSR ecx, uint32_t edx,uint32_t eax){
	__asm__(
	"wrmsr\n"
	:
	:"c"(ecx), "d"(edx), "a"(eax)
	);
}
