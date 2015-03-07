[BITS 32]
[SECTION .data]

NUMBER_OF_HANDLERS EQU 128

global _sizeOfIntEntries:
_sizeOfIntEntries:
dd intEntrySetEnd - intEntrySetBegin

global _numberOfIntEntries
_numberOfIntEntries:
	dd NUMBER_OF_HANDLERS

global _intEntryBaseOffset
_intEntryBaseOffset:
	dd baseAddressData - intEntrySetBegin

global _processorLocalAddressOffset
_processorLocalAddressOffset:
	dd processorLocal - intEntrySetBegin

global _intEntriesTemplate:
_intEntriesTemplate:
intEntrySetBegin:
; interrupt vector table
%assign n 0
%rep NUMBER_OF_HANDLERS
interruptEntry%[n]:
	dd entry%[n] - intEntrySetBegin ; entry offset
	dd 0xffffffff ; handler address
	dd 0xffffffff ; vector address
	dd 0 ; handler parameter
%assign n n+1
%endrep
processorLocal:
	dd 0
nestLevel:
	dd 0

%assign n 0
%rep NUMBER_OF_HANDLERS
entry%[n]:
%if n!=8 && n!=10 && n!=11 && n!=13 && n!=14 && n!=17
	push DWORD 0 ; for interrupt without error code
%endif
	push eax
	mov eax, interruptEntry%[n] - intEntrySetBegin
	jmp generalEntry
%assign n n+1
%endrep

getBase:
	db 0xba ; opcode: mov edx, ...
baseAddressData:
	dd 0x90909090 ; operand: ... 0x90909090
	ret

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

	call getBase
	add eax, edx
	push DWORD [edx + nestLevel - intEntrySetBegin]
	add DWORD [edx + nestLevel - intEntrySetBegin], 1
	push DWORD [edx + processorLocal - intEntrySetBegin]
	push DWORD [eax + 8] ; vector address
	push DWORD [eax + 12] ; parameter

	call [eax + 4] ; handler

	add esp, 16

	call getBase
	cli
	sub DWORD [edx + nestLevel - intEntrySetBegin], 1
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
intEntrySetEnd:
