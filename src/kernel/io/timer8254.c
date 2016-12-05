#include"interrupt/handler.h"
#include"interrupt/controller/pic.h"
#include"ioservice.h"
#include"common.h"
#include"kernel.h"
#include"assembly/assembly.h"

enum{
	TIMER_COMMAND = 0x43,
	TIMER_DATA0 = 0x40
};

#define TIMER_8254_FREQUENCY (1193182)

void setTimer8254Frequency(unsigned frequency){
	/*
	bit 7~6: channel = 00
	bit 5~4: low and high byte = 11
	bit 3~1: mode 2 (rate generator) = 010
	bit 0: 16bit binary = 0, BCD = 1
	*/
	const unsigned ticks = (TIMER_8254_FREQUENCY + frequency / 2) / frequency;
	assert(ticks < 65536);
	out8(TIMER_COMMAND, 0x34);
	out8(TIMER_DATA0, ticks & 255);
	out8(TIMER_DATA0, (ticks / 256) & 255);
}

void setTimer8254OneShot(unsigned interval){
	/*
	channel = 00
	low and high bit = 11
	operating mode = 0
	BCD mode = 0
	*/
	const unsigned ticks = (TIMER_8254_FREQUENCY * (uint64_t)interval) / 1000;
	assert(ticks < 65536);
	out8(TIMER_COMMAND, 0x30);
	out8(TIMER_DATA0, ticks & 255);
	out8(TIMER_DATA0, (ticks / 256) & 255);
}

uint16_t readTimer8254Count(void){
	/*
	bit 7~6: channel = 00
	bit 5~4: latch count value = 00
	bit 3~0: unused
	*/
	out8(TIMER_COMMAND, 0x0);
	uint8_t lo = in8(TIMER_DATA0);
	uint8_t hi = in8(TIMER_DATA0);
	return lo + (((uint16_t)hi) << 8);
}
