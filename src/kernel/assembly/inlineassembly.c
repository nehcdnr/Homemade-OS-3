
#include"assembly.h"

#define GET_REGISTER(T,F,R) \
T get##F(void){\
	T value = 0;\
	__asm__(\
	"mov %%"#R", %0\n"\
	:"=a"(value)\
	:\
	);\
	return value;\
}

GET_REGISTER(uint16_t, CS, cs)
GET_REGISTER(uint16_t, DS, ds)
GET_REGISTER(uint32_t, CR0, cr0)
GET_REGISTER(uint32_t, CR2, cr2)
GET_REGISTER(uint32_t, CR3, cr3)
GET_REGISTER(uint32_t, CR4, cr4)

#undef GET_REGISTER

#define SET_REGISTER(T,F,R)\
void set##F(T value){\
	__asm__(\
	"mov %0, %%"#R"\n"\
	:\
	:"a"(value)\
	);\
}

SET_REGISTER(uint32_t, CR0, cr0)
SET_REGISTER(uint32_t, CR3, cr3)

#undef SET_REGISTER

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
