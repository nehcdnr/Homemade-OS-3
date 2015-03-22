[BITS 32]
[SECTION .text]

extern _startTask
; uint32_t initTaskStack(uint32_t eFlags, uint32_t eip, uint32_t esp0)
global _initTaskStack
_initTaskStack:
	mov eax, [esp + 12] ; esp0
	mov edx, [esp + 8] ; eip
	mov [eax - 4], edx
	mov DWORD [eax - 8], _startTask
	mov edx, [esp + 4] ; eFlags
	mov [eax - 12], edx
	sub eax, 12 + 32 ; eip, startTask, pushf, pushad
	ret

; void contextSwitch(uint32_t *oldTaskESP0, uint32_t newTaskESP0)
global _contextSwitch
_contextSwitch:
	pushf ; 4 bytes
	pushad ; 8*4 bytes
	mov eax, [esp + 40] ; oldTaskESP0
	mov [eax], esp
	mov esp, [esp + 44] ; newTaskESP0
	popad
	popf
	ret
