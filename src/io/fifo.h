#include"std.h"
typedef struct MemoryManager MemoryManager;
typedef struct FIFO FIFO;

void writeFIFO(FIFO *fifo, uintptr_t data);
int readFIFO(FIFO *fifo, uintptr_t *data);
FIFO *createFIFO(MemoryManager *m, int maxLength);
