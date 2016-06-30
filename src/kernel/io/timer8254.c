#include"interrupt/handler.h"
#include"interrupt/controller/pic.h"
#include"ioservice.h"
#include"common.h"
#include"kernel.h"
#include"assembly/assembly.h"

void setTimer8254Frequency(unsigned frequency){
	enum{
		TIMER_COMMAND = 0x43,
		TIMER_DATA0 = 0x40
	};
	/*
	bit 7~6: channel = 00
	bit 5~4: low and high byte = 11
	bit 3~1: mode 2 (rate generator) = 010
	bit 0: 16bit binary = 0, BCD = 1
	*/
	const unsigned period = (1193182 + frequency / 2) / frequency;
	assert(period < 65536);
	out8(TIMER_COMMAND, 0x34);
	out8(TIMER_DATA0, period & 255);
	out8(TIMER_DATA0, (period / 256) & 255);
}
