#ifndef ASSEMBLY_H_INCLUDED
#define ASSEMBLY_H_INCLUDED

#include<std.h>
#include<common.h>

#define hlt() do{__asm__("hlt\n");}while(0)
#define cli() do{__asm__("cli\n");}while(0)
#define sti() do{__asm__("sti\n");}while(0)
#define nop() do{__asm__("nop\n");}while(0)
#define pause() do{__asm__("pause\n");}while(0)

uint32_t getEBP(void);

uint16_t getCS(void);
uint16_t getDS(void);

uint32_t getCR0(void);
void setCR0(uint32_t value);
uint32_t getCR2(void);
uint32_t getCR3(void);
void setCR3(uint32_t value);
uint32_t getCR4(void);

uint8_t in8(uint16_t port);
uint16_t in16(uint16_t port);
uint32_t in32(uint16_t port);
void out8(uint16_t port, uint8_t value);
void out16(uint16_t port, uint16_t value);
void out32(uint16_t port, uint32_t value);
uint8_t xchg8(volatile uint8_t *a, uint8_t b);
uint32_t xchg32(volatile uint32_t *a, uint32_t b);
void lock_add32(volatile uint32_t *a, uint32_t b);
//if(*dst != cmp)cmp = *dst
//else *dst = src
//return cmp
uint32_t lock_cmpxchg32(volatile uint32_t *dst, uint32_t cmp, uint32_t src);
#define ATOMIC_READ_32(ADDRESS) lock_cmpxchg32((ADDRESS), 0, 0)
#define ATOMIC_WRITE_32(ADDRESS, VALUE) xchg32((ADDRESS), (VALUE))

typedef union EFlags{
	uint32_t value;
	struct{
		uint32_t
		carry: 1,
		reserve1: 1,
		parity: 1,
		reserve0_0: 1,
    // 4
		auxCarry: 1,
		reserve0_1: 1,
		zero: 1,
		sign: 1,
		// 8
		trap: 1,
		interrupt: 1,
		direction: 1,
		overflow: 1,
		// 12
		ioPrivilege: 2,
		nestedTask: 1,
		reserved0_2: 1,
		// 16
		resume: 1,
		virtual8086: 1,
		alignmentCheck: 1,
		virtualInterrupt: 1,
		// 20
		virtualInterruptPending: 1,
		id: 1,
		reserved0_3: 10;
	}bit;
}EFlags;
static_assert(sizeof(EFlags) == 4);
EFlags getEFlags(void);

int cpuid_isSupported(void);
int cpuid_hasAPIC(void);
int cpuid_getInitialAPICID(void);

enum MSR{
	IA32_APIC_BASE = 0x1b
};

void rdmsr(enum MSR ecx, uint32_t *edx, uint32_t *eax);
void wrmsr(enum MSR ecx, uint32_t edx, uint32_t eax);

#endif
