ENTRY(_entry)
SECTIONS{
	. = 0xbe00;
	.entry : {*(.entry)}
	.text : {*(.text)}
	.rdata : {*(.rdata)}
	.data : {*(.data)}
	.bss : {*(.bss)}
	.drectve : {*(.drectve)}

}
