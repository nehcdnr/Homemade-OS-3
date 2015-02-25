
#define hlt() do{__asm__("hlt\n");}while(0)
#define cli() do{__asm__("cli\n");}while(0)
#define sti() do{__asm__("sti\n");}while(0)
#define nop() do{__asm__("nop\n");}while(0)

unsigned short getCS(void);
unsigned short getDS(void);
unsigned int getEBP(void);
unsigned short getCR3(void);
void setCR3(unsigned value);
unsigned short getCR0(void);
void setCR0(unsigned value);
unsigned char in(unsigned short port);
void out(unsigned short port, unsigned char value);
unsigned xchg(volatile unsigned *a, unsigned b);
#define EFLAGS_IF (1<<9)
unsigned getEFlags(void);

int cpuid_isSupported(void);
unsigned cpuid_HasOnChipAPIC(void);

enum MSR{
	IA32_APIC_BASE = 0x1b
};

void rdmsr(enum MSR ecx, unsigned *edx,unsigned *eax);
void wrmsr(enum MSR ecx, unsigned edx,unsigned eax);
