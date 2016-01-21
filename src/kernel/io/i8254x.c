#include"io.h"
#include"task/task.h"
#include"file/file.h"

void i8254xDriver(void){
	uintptr_t pci = enumeratePCI(0x02000000, 0xffffff00);
	if(pci == IO_REQUEST_FAILURE){
		printk("enum PCI failed");
		systemCall_terminate();
	}
	while(1){
		PCIConfigRegisters regs;
		if(nextPCIConfigRegisters(pci, &regs, sizeof(regs.regs0)) != sizeof(regs.regs0)){
			break;
		}
		PCIConfigRegisters0 *const regs0 = &regs.regs0;
		printk("%d %d\n",regs0->interruptLine, regs0->interruptPIN);
	}
	syncCloseFile(pci);

	while(1)
		sleep(1000);

	systemCall_terminate();
}
