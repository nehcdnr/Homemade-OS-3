ENTRY(entry)
KERNEL_LINEAR_BEGIN_SYMBOL = 0xf0000000;
KERNEL_LINEAR_END_SYMBOL = 0xfffff000;
ASSERT(KERNEL_LINEAR_BEGIN_SYMBOL % 4096 == 0, "kernel linear address not aligned to page");
ASSERT(KERNEL_LINEAR_END_SYMBOL % 4096 == 0, "kernel linear address not aligned to page");

MEMORY{
	lma (RWXAI): ORIGIN = 0xbe00, LENGTH = 0xf0000000
}

SECTIONS{
	. = 0xbe00;
	.real . : {*(.real)} AT > lma
	.v8086 . : {*(.v8086)} AT > lma

	. += KERNEL_LINEAR_BEGIN_SYMBOL;
	.text . : {*(.text)} AT > lma 
	.rodata . : {*(.rodata)} AT > lma
	.rdata . : {*(.rdata)} AT > lma
	.data . : {*(.data)} AT > lma
	.drectve . : {*(.drectve)} AT > lma
	.bss . : {
		_bss_linear_start = .;
		*(.bss)
		_bss_linear_end = .;
	} AT > lma

	/DISCARD/ : {*(.comment .eh_frame)}
}
