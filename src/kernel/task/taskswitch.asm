[BITS 32]
[SECTION .text]

extern startTask
; uint32_t initTaskStack(uint32_t eFlags, uint32_t eip, uint32_t esp0)
global initTaskStack
initTaskStack:
	mov eax, [esp + 12] ; esp0
	mov edx, [esp + 8] ; eip
	mov ecx, [esp + 4] ; eFlags
	mov [eax - 4], edx
	mov DWORD [eax - 8], startTask
	mov [eax - 12], ecx
	; return new esp
	sub eax, 12 + 32 ; eip, startTask, pushfd, pushad
	ret

; void contextSwitch(uint32_t *oldTaskESP0, uint32_t *newTaskESP0, uint32_t newCR3)
global contextSwitch
contextSwitch:
	pushfd ; 4 bytes
	pushad ; 8*4 bytes
	mov edx, [esp + 48] ; newCR3
	mov ecx, [esp + 44] ; newTaskESP0
	mov eax, [esp + 40] ; oldTaskESP0
	mov [eax], esp
	mov esp, ecx
	mov cr3, edx
	popad ; esp is ignored
	popfd
	ret
