#include<std.h>

#define hlt() do{__asm__("hlt\n");}while(0)
#define cli() do{__asm__("cli\n");}while(0)
#define sti() do{__asm__("sti\n");}while(0)
#define nop() do{__asm__("nop\n");}while(0)

uint16_t getCS(void);
uint16_t getDS(void);
uint32_t getEBP(void);
uint32_t getCR3(void);
void setCR3(uint32_t value);
uint32_t getCR0(void);
void setCR0(uint32_t value);
uint8_t in(uint16_t port);
void out(uint16_t port, uint8_t value);
uint32_t xchg(volatile uint32_t *a, uint32_t b);
#define EFLAGS_IF (1<<9)
uint32_t getEFlags(void);

int cpuid_isSupported(void);
int cpuid_hasAPIC(void);

enum MSR{
	IA32_APIC_BASE = 0x1b
};

void rdmsr(enum MSR ecx, uint32_t *edx, uint32_t *eax);
void wrmsr(enum MSR ecx, uint32_t edx, uint32_t eax);
