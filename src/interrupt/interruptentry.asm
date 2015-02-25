[BITS 32]
[SECTION .text]

NUMBER_OF_HANDLERS EQU 128
%assign n 0
%rep NUMBER_OF_HANDLERS
entry%[n]:
%if n!=8 && n!=10 && n!=11 && n!=13 && n!=14 && n!=17
	push DWORD 0 ; for interrupt without error code
%endif
	push eax
	mov eax, interruptEntry%[n]
	jmp generalEntry
%assign n n+1
%endrep

generalEntry:
	push ecx
	push edx
	push ebx
	push ebp
	push esi
	push edi
	push ds ; esp -= 4
	; mov WORD [esp + 2], 0
	push es
	; mov WORD [esp + 2], 0
	push fs
	; mov WORD [esp + 2], 0
	push gs
	; mov WORD [esp + 2], 0
	push DWORD [eax] ; vector address
	call [eax + 4]
	add esp, 4
	pop gs
	pop fs
	pop es
	pop ds
	pop edi
	pop esi
	pop ebp
	pop ebx
	pop edx
	pop ecx
	pop eax
	add esp, 4 ; pop error code
	iretd

[SECTION .data]

; interrupt vector table length
global _asmIntEntryCount
_asmIntEntryCount:
	dd NUMBER_OF_HANDLERS

; interrupt vector table
global _asmIntEntry
_asmIntEntry:
%assign n 0
%rep NUMBER_OF_HANDLERS
interruptEntry%[n]:
	dd 0xffffffff ; vector address
	dd 0xffffffff ; handler address
	dd entry%[n]
%assign n n+1
%endrep
