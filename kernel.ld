ENTRY(_entry)
_KERNEL_LINEAR_BASE_SYMBOL = 0xf0000000-0xf0000000;
_KERNEL_LINEAR_END_SYMBOL = 0xfffff000-0xf0000000;
ASSERT(_KERNEL_LINEAR_BASE_SYMBOL % 4096 == 0, "kernel linear address not aligned to page");
ASSERT(_KERNEL_LINEAR_END_SYMBOL % 4096 == 0, "kernel linear address not aligned to page");

SECTIONS{
	. = 0xbe00;
	.entry : {*(.entry)}
	.text : {*(.text)}
	.rdata : {*(.rdata)}
	.data : {*(.data)}
	.drectve : {*(.drectve)}
	.bss : {
		__bss_physical_start = .;
		*(.bss)
		__bss_physical_end = .;
	}
}
