#include"bios.h"
#include"interrupt/handler.h"
#include"task/task.h"
#include"fifo.h"
#include"common.h"
#include"kernel.h"

#define DEFAULT_EBDA_BEGIN (0x9fc00)
uintptr_t findAddressOfEBDA(uintptr_t kernelLinearBegin){
	uintptr_t address = ((*(uint16_t*)(kernelLinearBegin + 0x40e))<<4);
	if(address >= EBDA_END || address < 0x80000){
		printk("suspicious EBDA address: %x\n", address);
		address = DEFAULT_EBDA_BEGIN;
	}
	printk("address of EBDA = %x\n", address);
	return kernelLinearBegin + address;
}

uint8_t checksum(const void *data, size_t size){
	const uint8_t *d = (const uint8_t*)data;
	uint8_t sum = 0;
	while(size--){
		sum+=*d;
		d++;
	}
	return sum;
}

void *searchString(const char *string, uintptr_t beginAddress, uintptr_t endAddress){
	uintptr_t a;
	uintptr_t b = strlen(string);
	for(a = beginAddress; a + b <= endAddress; a++){
		if(strncmp(string, (char*)a, b) == 0)
			return (void*)a;
	}
	return NULL;
}

void *searchStructure(
	size_t structureSize,
	const char *string,
	uintptr_t searchBegin,
	uintptr_t searchEnd
){
	void *s = NULL;
	while(s == NULL){
		s = searchString(string, searchBegin, searchEnd);
		if(s == NULL)
			break;
		if(checksum((uint8_t*)s, structureSize) != 0){
			searchBegin = 1 + (uintptr_t)s;
			s = NULL;
		}
	}
	return s;
}
