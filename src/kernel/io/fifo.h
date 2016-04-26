#include"std.h"
typedef struct FIFO FIFO;

// return 1 if written, 0 if not
int writeFIFO(FIFO *fifo, void *data);
// remove oldest data if buffer is full. return 1 if buffer length changed
int overwriteFIFO(FIFO *fifo, void *data, void *overwrittenData);

// return 1 if FIFO has data, 0 if FIFO is empty
int peekFIFO(FIFO *fifo, void *data);
void readFIFO(FIFO *fifo, void *data);
int readFIFONonBlock(FIFO *fifo, void *data);
uintptr_t getElementSize(FIFO *fifo);
uintptr_t getDataLength(FIFO *fifo);
FIFO *createFIFO(uintptr_t maxLength, uintptr_t elementSize);
void deleteFIFO(FIFO *fifo);

