[BITS 16]
[SECTION .text]
global _callBIOS
_callBIOS:
	mov eax, 1 ; SYSCALL_TASK_DEFINED
	int 126 ; void systemCall(enum SystemCall eax);
	int 0x10 ; bios
	shl eax, 16
	mov ax, bx
	mov ebx, eax
	jmp _callBIOS
