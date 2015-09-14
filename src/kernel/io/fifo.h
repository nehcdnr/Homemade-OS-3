#include"std.h"
typedef struct FIFO FIFO;

// return 1 if written, 0 if not
int writeFIFO(FIFO *fifo, uintptr_t data);
// remove oldest data if buffer is full. return 1 if buffer length changed
int overwriteFIFO(FIFO *fifo, uintptr_t data);

// return 1 if FIFO has data, 0 if FIFO is empty
int peekFIFO(FIFO *fifo, uintptr_t *data);
int readFIFO(FIFO *fifo, uintptr_t *data);
FIFO *createFIFO(int maxLength);
