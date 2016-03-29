[BITS 16]
KERNEL_ENTRY EQU 0xbe00

	org 0x7c00
	jmp main
	; resb gives warning
	times 3+$$-$ db 0 ; jmp command has to be 3 bytes
	db "01234567" ; 8-byte OEM name
	dw 512 ; number of bytes in a sector
	db 1 ; number of sectors in a cluster
	dw 1 ; index of boot sectors
	db 2
	dw 224
	dw 2880
	db 0xf0
	dw 9
	dw 18
	dw 2
	dd 0
	dd 2880
	db 0, 0, 0x29
	dd 0
	db "HomemadeOS3"
	db "FAT12   "
main:
	; initialize registers
	mov ax, 0
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	; skip if kernel has been loaded by BSP
	cmp BYTE [kernelloadedflag], 0
	jne KERNEL_ENTRY
	mov BYTE [kernelloadedflag], 1
	; set AP entry code
	mov si, apentrycode_begin
	mov di, 0x7000
copyapentrycodeloop:
	mov al, [si]
	mov [di], al
	add si, 1
	add di, 1
	cmp si, apentrycode_end
	jb copyapentrycodeloop
	; begin of loading sectors from disk
	mov si, 0 ; failure count
	mov di, 1 ; load count

	mov esp, 0x7000
	mov ebp, 0x7000
	mov ch, 0 ; cylinder number
	mov dh, 0 ; head number
	mov cl, 2 ; sector number, starting with 1
	mov bx, 0x7c00 + 512 ; es:bx = buffer
	mov dl, 0 ; drive number
loadloop:
	cmp di, 512 ; load 512 sectors = 256kB including 16kB FAT
	jae KERNEL_ENTRY
	mov al, 1 ; number of sectors to read
	mov ah, 0x02 ; read disk
	int 0x13
	jnc disksuccess ; if success
diskfailure: ; if failure, al, ah are results
	add si, 1
	cmp si, 1
	jmp showerror
	;reset disk
	mov ah, 0 ; ah = 0
	; assume dl = 0 = drive number
	int 0x13
	jc diskfailure ; if failure, ah = 0
	jmp loadloop
disksuccess:
	add di, 1 ; load count++
	mov ax, es ; es:bs += 512
	add ax, 512/16
	mov es, ax
	add cl, 1 ; sector number
	cmp cl, 19
	jb loadloop
	mov cl, 1
	add dh, 1 ; head number
	cmp dh, 2
	jb loadloop
	mov dh, 0
	add ch, 1 ; cylinder number
	jmp loadloop
	; end of loading sectors

showerror:
	mov ax, cs
	mov ds, ax
	mov es, ax
	mov ax, errormsg
	mov bp, ax ; es:bx = string address
	mov cx, errormsg_end-errormsg ; length
	mov ax, 0x1301 ; write string containing special characters
	mov bx, 0x000c ; color
	mov dx, 0x0000 ; row&column
	int 0x10
hltloop:
	hlt
	jmp hltloop 

errormsg:
	db "floppy disk loader failed!"
errormsg_end:

kernelloadedflag:
	db 0
apentrycode_begin:
	jmp WORD 0:main
apentrycode_end:

	times 510+($$-$) db 0
	db 0x55
	db 0xaa
