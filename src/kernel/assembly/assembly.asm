[BITS 32]
[SECTION .text]

global xchg8
xchg8:
	xor eax, eax
	mov edx, [esp + 4]
	mov al, [esp + 8]
	xchg [edx], al
	ret

global lock_add32
lock_add32:
	mov edx, [esp + 4]
	mov eax, [esp + 8]
	lock add [edx], eax
	ret

global lock_cmpxchg32
lock_cmpxchg32:
	mov edx, [esp + 4]
	mov eax, [esp + 8]
	mov ecx, [esp + 12]
	lock cmpxchg [edx], ecx
	ret

global getEFlags
getEFlags:
	pushfd
	pop eax
	ret

global getEBP
getEBP:
	mov eax, ebp
	ret

global cpuid_isSupported
cpuid_isSupported:
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
global lldt
lldt:
	mov ax, [esp + 4]
	mov [esp + 6], ax
	lldt [esp + 6]
	ret

; lgdt(limit, base, codeSegment, dataSegment)
global lgdt
lgdt:
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
