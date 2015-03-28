#include"std.h"
typedef struct FIFO FIFO;

void writeFIFO(FIFO *fifo, uintptr_t data);

// return 1 if FIFO has data, 0 if FIFO is empty
int peekFIFO(FIFO *fifo, uintptr_t *data);
int readFIFO(FIFO *fifo, uintptr_t *data);
FIFO *createFIFO(int maxLength);
