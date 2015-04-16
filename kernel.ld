ENTRY(_entry)
KERNEL_VIRTUAL_ADDRESS = 0xf0000000;

ASSERT(KERNEL_VIRTUAL_ADDRESS % (1<<22) == 0, "kernel not aligned to 4MB")
SECTIONS{
	. = 0xbe00;
	.entry : {*(.entry)}
	.text : {*(.text)}
	.rdata : {*(.rdata)}
	.data : {*(.data)}
	.bss : {*(.bss)}
	.drectve : {*(.drectve)}
}
