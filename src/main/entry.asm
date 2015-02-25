[BITS 16]
[SECTION .entry]
; org 0xbe00
global _entry
_entry:
; assume cs = ds = 0
; ss, esp, ebp = uninitialized
; --------get address range--------

	cmp BYTE [memorymaploadedflag], 0
	jne entry2
	mov BYTE [memorymaploadedflag], 1
	; set BSP stack
	mov ax, 0
	mov ss, ax
	mov esp, 0x7000
	mov ebp, 0x7000
	; bios 0x13 0xe820
	mov DWORD [_addressRangeCount], 0
	mov ebx, 0 ; next call
memorymaploop:
	mov eax, 0xe820 ; function
	mov dx, ss
	mov es, dx
	sub esp, 24
	lea di, [esp] ; es:di = buffer
	mov ecx, 24
	mov edx, 0x534d4150 ; "SMAP"
	int 0x15
	jc memorymapend ; if error or after the last entry
	cmp eax, 0x534d4150 ; "SMAP"
	jne memorymapend ; if error
	add DWORD [_addressRangeCount], 1
	cmp ebx, 0
	jne memorymaploop ; if not the last entry
	sub esp, 24
memorymapend:
	add esp, 24
	mov [_addressRange], esp
	mov ebp, esp
	jmp entry2

memorymaploadedflag:
	db 0

	global _addressRange
_addressRange: ; memory.c
	dd 0
	global _addressRangeCount
_addressRangeCount:
	dd 0

; --------set temporary GDT--------
entry2:
	; 1. disable interrupt
	cli
	; 2. set temporary gdt
	lgdt [lgdtparameter]
	; 3. enable a20 line
	in al, 0x92
	or al, 2
	out 0x92, al
	; 4. set cr0
	mov eax, cr0
	and eax, 0x7fffffff ; paging=0
	or eax, 1 ; protected mode=1
	mov cr0, eax
	jmp flushpipeline
	flushpipeline:
	; 5. reload data segments
	mov ax, 8*2
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	jmp DWORD 8:entry3 ; reload code segment and go to entry.asm

	align 8
gdt0:
	dw 0 ; limit (0~16)
	dw 0 ; base (0~16)
	db 0 ; base (16~24)
	db 0 ; access
	db 0 ; limit (16~20), flag
	db 0 ; base (24~32)
gdt1:
	dw 0xffff
	dw 0
	db 0
	db 0x98 ; present=1, privilege=00, 1, executable=1, low-privilege accessible=1, readable=0, 0
	db 0xcf ; 4kB unit=1, 32bit=1, 0, 0
	db 0
gdt2:
	dw 0xffff
	dw 0
	db 0
	db 0x92 ; present=1, privilege=00, 1, executable=0, grow down=0, writable=1, 0
	db 0xcf
	db 0
gdtend:

lgdtparameter:
	dw gdtend-gdt0-1; limit
	dd gdt0 ; base

; --------set stack registers if this is AP--------
[BITS 32]
extern _bspEntry ; main.c
extern _apEntry ; main.c
extern _initialESP ; lapic.c

entry3:
acquirelock:
	mov eax, 0
	lock xchg [spinlock], eax
	cmp eax, 1
	je criticalsection
waitlock:
	pause
	cmp DWORD [spinlock], 1
	je acquirelock
	jmp waitlock

criticalsection:
	mov ecx, [initnumber]
	add DWORD [initnumber], 1
	cmp ecx, 0
	je releaselock
initapstack:
	mov ebx, [_initialESP]
	mov esp, [ebx + ecx * 4]
	mov ebp, esp

releaselock:
	mov eax, 1
	lock xchg [spinlock], eax

; go to main.c
cmp ecx, 0
je _bspEntry
jmp _apEntry

spinlock:
	dd 1
initnumber:
	dd 0
