ENTRY(_entry)
_KERNEL_LINEAR_BEGIN_SYMBOL = 0xf0000000;
_KERNEL_LINEAR_END_SYMBOL = 0xfffff000;
ASSERT(_KERNEL_LINEAR_BEGIN_SYMBOL % 4096 == 0, "kernel linear address not aligned to page");
ASSERT(_KERNEL_LINEAR_END_SYMBOL % 4096 == 0, "kernel linear address not aligned to page");

SECTIONS{
	. = 0xbe00;
	.real 0xbe00 : {*(.real)}
	.v8086 . : {*(.v8086)}
	.text . + _KERNEL_LINEAR_BEGIN_SYMBOL : {*(.text)}
	.rdata : {*(.rdata)}
	.data : {*(.data)}
	.drectve : {*(.drectve)}
	.bss : {
		__bss_linear_start = .;
		*(.bss)
		__bss_linear_end = .;
	}
}
