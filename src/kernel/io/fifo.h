#include"std.h"

// see fifo.c
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

// see fifofile.c
/*
typedef struct FIFOList FIFOList;
int writeFIFOList(FIFOList *fifo, void *data, uintptr_t dataSize);
uintptr_t readFIFOList(FIFOList *fifo, void *data, uintptr_t dataSize);
// Each write operation corresponds to one read operation
// The data is truncated if read buffer size < write size
FIFOList *createFIFOList(void);
void deleteFIFOList(FIFOList *fifoList);
*/
