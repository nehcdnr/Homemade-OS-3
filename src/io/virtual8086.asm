[BITS 16]
[SECTION .text]
global _callBIOS
_callBIOS:
	mov eax, 1
	int 126 ; void systemCall(enum SystemCall eax);
	push WORD '3' + 256*'4'
	push WORD '1' + 256*'2'
	mov ax, ss
	mov es, ax
	mov bp, sp ; es:bp = string address
	mov cx, 4 ; length
	mov ax, 0x1301 ; write string containing special characters
	mov bx, 0x000c ; color
	mov dx, 0x0000 ; row&column
	int 0x10
	add sp, 4
	jmp _callBIOS
