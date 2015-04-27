[BITS 32]
[SECTION .text]

global _xchg
_xchg:
	mov edx, [esp + 4]
	mov eax, [esp + 8]
	xchg [edx], eax
	ret

global _getEFlags
_getEFlags:
	pushfd
	pop eax
	ret

global _getEBP
_getEBP:
	mov eax, ebp
	ret

global _getEIP
_getEIP:
	mov eax, [esp]
	ret

global _cpuid_isSupported
_cpuid_isSupported:
	pushfd
	mov eax, [esp]
	xor eax, 0x200000
	push eax
	popfd
	pushfd
	pop eax
	popfd
	shr eax, 21
	and eax, 1
	ret

; lldt(limit, base)
global _lldt
_lldt:
	mov ax, [esp + 4]
	mov [esp + 6], ax
	lldt [esp + 6]
	ret

; lgdt(limit, base, codeSegment, dataSegment)
global _lgdt
_lgdt:
	push ebp
	mov ebp, esp
	sub esp, 8

	mov DWORD [esp + 0], reloadcs
	mov ax, [ebp + 16] ; codeSegment
	mov [esp + 4], ax

	mov dx, [ebp + 8] ; limit
	mov [ebp + 10], dx

	lgdt [ebp + 10]

	jmp [esp + 0]
reloadcs:
	mov ax, [ebp + 20] ; dataSegment

	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	add esp, 8
	pop ebp
	ret
