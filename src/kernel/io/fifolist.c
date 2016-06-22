
#include"memory/memory.h"
#include"common.h"


typedef struct FIFOElement{
	struct FIFOElement *next;
	uintptr_t bufferSize;
	uint8_t buffer[];
}FIFOElement;
