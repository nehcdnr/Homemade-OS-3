[BITS 32]
[SECTION .data]

align 4096
NUMBER_OF_HANDLERS EQU 128

global _numberOfIntEntries
_numberOfIntEntries:
	dd NUMBER_OF_HANDLERS

global _sizeOfIntEntries
_sizeOfIntEntries:
	dd intEntriesEnd - _intEntries

global _intEntriesAddress

global _intEntries:
_intEntries:
; interrupt vector table
%assign n 0
%rep NUMBER_OF_HANDLERS
interruptEntry%[n]:
	dd entry%[n] - _intEntries ; entry offset
	dd 0xffffffff ; handler address
	dd 0xffffffff ; vector address
	dd 0 ; handler parameter
%assign n n+1
%endrep

%assign n 0
%rep NUMBER_OF_HANDLERS
entry%[n]:
%if n!=8 && n!=10 && n!=11 && n!=12 && n!=13 && n!=14 && n!=17
	push DWORD 0 ; for interrupt without error code
%endif
	push eax
	mov eax, interruptEntry%[n] - _intEntries
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
	mov bx, ss
	mov ds, bx
	mov es, bx
	mov fs, bx
	mov gs, bx

	db 0xba ; opcode: mov edx,
_intEntriesAddress:
	dd _intEntries ; operand: _intEntries

	add eax, edx
	push DWORD [eax + 8] ; vector address
	push DWORD [eax + 12] ; parameter
	mov edx, esp
	push edx ; &InterruptParam
	call [eax + 4] ; handler

; see taskmanager.c.h
global _returnFromInterrupt
_returnFromInterrupt:
	cli
	add esp, 12
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
intEntriesEnd:
