
#include"assembly.h"

unsigned short getCS(void){
	unsigned short c;
	__asm__(
	"mov %%cs, %0\n"
	:"=a"(c)
	:
	);
	return c;
}

unsigned short getDS(void){
	unsigned short d;
	__asm__(
	"mov %%ds, %0\n"
	:"=a"(d)
	:
	);
	return d;
}

unsigned short getCR3(void){
	unsigned value = 0;
	__asm__(
	"mov %%cr3, %0\n"
	:"=a"(value)
	:
	);
	return value;
}

void setCR3(unsigned value){
	__asm__(
	"mov  %0, %%cr3\n"
	:
	:"a"(value)
	);
}

unsigned short getCR0(void){
	unsigned value = 0;
	__asm__(
	"mov %%cr0, %0\n"
	:"=a"(value)
	:
	);
	return value;
}

void setCR0(unsigned value){
	__asm__(
	"mov %0, %%cr0\n"
	:
	:"a"(value)
	);
}

unsigned char in(unsigned short port){
	unsigned char value;
	__asm__(
	"in %1, %0\n"
	:"=a"(value)
	:"d"(port)
	);
	return value;
}

void out(unsigned short port, unsigned char value){
	__asm__(
	"out %1, %0\n"
	:
	:"d"(port), "a"(value)
	);
}

static void cpuid(unsigned *eax, unsigned *ebx, unsigned *ecx, unsigned *edx){
	__asm__(
	"cpuid\n"
	:"=a"(*eax),"=b"(*ebx),"=c"(*ecx),"=d"(*edx)
	:"a"(*eax),"c"(*ecx)
	);
}

unsigned cpuid_HasOnChipAPIC(void){
	unsigned eax, ebx, ecx, edx;
	eax = 1;
	ebx = ecx = edx = 0;
	cpuid(&eax, &ebx, &ecx, &edx);
	return (edx >> 9) & 1;
}

void rdmsr(enum MSR ecx, unsigned *edx,unsigned *eax){
	__asm__(
	"rdmsr\n"
	:"=d"(*edx),"=a"(*eax)
	:"c"(ecx)
	);
}

void wrmsr(enum MSR ecx, unsigned edx,unsigned eax){
	__asm__(
	"wrmsr\n"
	:
	:"c"(ecx), "d"(edx), "a"(eax)
	);
}
