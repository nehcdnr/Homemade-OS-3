#include"systemcall.h"
#include"common.h"

#define I_REG1(ARG) "a"(systemCallNumber)
#define I_REG2(ARG) I_REG1(ARG), "d"(ARG##1)
#define I_REG3(ARG) I_REG2(ARG), "c"(ARG##2)
#define I_REG4(ARG) I_REG3(ARG), "b"(ARG##3)
#define I_REG5(ARG) I_REG4(ARG), "S"(ARG##4)
#define I_REG6(ARG) I_REG5(ARG), "D"(ARG##5)

#define O_REG1(ARG) "=a"(r)
#define O_REG2(ARG) O_REG1(ARG), "=d"(ARG##1)
#define O_REG3(ARG) O_REG2(ARG), "=c"(ARG##2)
#define O_REG4(ARG) O_REG3(ARG), "=b"(ARG##3)
#define O_REG5(ARG) O_REG4(ARG), "=S"(ARG##4)
#define O_REG6(ARG) O_REG5(ARG), "=D"(ARG##5)

#define SYSTEM_CALL_VECTOR_STRING TO_STRING(SYSTEM_CALL_VECTOR)

#define SYSCALL_ASM "int $"SYSTEM_CALL_VECTOR_STRING"\n"
#define SYSCALL1_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG1(ARG)
#define SYSCALL2_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG2(ARG)
#define SYSCALL3_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG3(ARG)
#define SYSCALL4_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG4(ARG)
#define SYSCALL5_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG5(ARG)
#define SYSCALL6_ASM(ARG) SYSCALL_ASM: O_REG1(ARG): I_REG6(ARG)
#define SYSCALL6RETURN_ASM(ARG) SYSCALL_ASM: O_REG6(ARG): I_REG6(ARG)

static_assert(sizeof(uint32_t) == sizeof(uintptr_t));

uintptr_t systemCall1(int systemCallNumber){
	uintptr_t r;
	__asm__(SYSCALL1_ASM(arg));
	return r;
}

uintptr_t systemCall2(int systemCallNumber, uintptr_t arg1){
	uintptr_t r;
	__asm__(SYSCALL2_ASM(arg));
	return r;
}

uintptr_t systemCall3(int systemCallNumber, uintptr_t arg1, uintptr_t arg2){
	uintptr_t r;
	__asm__(SYSCALL3_ASM(arg));
	return r;
}

uintptr_t systemCall4(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3){
	uintptr_t r;
	__asm__(SYSCALL4_ASM(arg));
	return r;
}

uintptr_t systemCall5(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
	uintptr_t arg4){
	uintptr_t r;
	__asm__(SYSCALL5_ASM(arg));
	return r;
}

uintptr_t systemCall6(int systemCallNumber, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
	uintptr_t arg4, uintptr_t arg5){
	uintptr_t r;
	__asm__(SYSCALL6_ASM(arg));
	return r;
}

uintptr_t systemCall6Return(int systemCallNumber, uintptr_t *arg1, uintptr_t *arg2, uintptr_t *arg3,
	uintptr_t *arg4, uintptr_t *arg5){
	uintptr_t r;
	__asm__(SYSCALL6RETURN_ASM(*arg));
	return r;
}
