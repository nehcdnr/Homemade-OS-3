ENTRY(_entry)
SECTIONS{
	. = 0xbe00;
	.entry : {*(.entry)}
	.text : {*(.text)}
	.rdata : {*(.rdata)}
	.data : {*(.data)}

	.drectve : {*(.drectve)}
	.bss : {*(.bss)}
}
