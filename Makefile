# required
NDEBUG=-DNDEBUG
NASM=./tool/nasm-2.11.06/nasm.exe
CC=gcc.exe
CFLAGS=-m32 -march=i586 -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes -fno-builtin -Isrc -O0
BFI=./tool/bfi10/bfi.exe

# optional
QEMU=./tool/Qemu-windows-2.2.0
#QEMU_IMG=$(QEMU)/qemu-img.exe
QEMU_I386=$(QEMU)/qemu-system-i386w.exe -L $(QEMU)/Bios -cpu pentium
BOCHS=./tool/bochs-p4-smp-2.6.7-win32
BOCHS_SMP=$(BOCHS)/bochsdbg-p4-smp.exe
VIRTUALBOX=C:\Program Files\Oracle\VirtualBox\VBoxManage.exe

# source
ASM_SRC=src\interrupt\interruptentry.asm src\assembly\assembly.asm src\main\entry.asm
C_SRC=$(shell dir /A-D /B /S src\*.c)
C_OBJ=$(C_SRC:.c=.o)
ASM_OBJ=$(ASM_SRC:.asm=.o)
OBJ=$(C_OBJ) $(ASM_OBJ)
BOOTLOADER_SRC=.\src\bootloader\floppyloader.asm

all: os3.img

os3.img: floppyloader.bin build/kernel.bin
	$(BFI) -t=6 -b=$< -f=$@ build

floppyloader.bin: $(BOOTLOADER_SRC)
	$(NASM) $< -o $@

build/kernel.bin: $(OBJ)
	ld -T kernel.ld -m i386pe --file-alignment 8 --section-alignment 8 -o kernel.o $(OBJ)
	objcopy -O binary kernel.o build/kernel.bin

.SUFFIXES: .o .asm
.asm.o:
	$(NASM) -f elf32 $< -o $@

clean:
	del floppyloader.bin os3.img build\kernel.bin kernel.o $(OBJ)

run: runqemu

runbochs: os3.img
	$(BOCHS_SMP) -q -f $(BOCHS)/bochsrc.bxrc

runqemu: os3.img
	$(QEMU_I386) -smp cpus=3,cores=2,threads=2 -m 128 -fda os3.img

runvbox: os3.img
	$(VIRTUALBOX) startvm HomemadeOS3 --type gui
