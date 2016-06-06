#include"multiprocessor/spinlock.h"

typedef struct{
	Spinlock lock;
	int value;
}ReferenceCount;

void initReferenceCount(ReferenceCount *rc, int value);

#define INITIAL_REFERENCE_COUNT(V) {INITIAL_SPINLOCK, (V)}

int addReference(ReferenceCount *rc, int changeValue);

#define INCREASE_REFERENCE(X) addReference(&(X)->referenceCount, 1)

#define DECREASE_REFERENCE(X) \
do{\
	if(addReference(&(X)->referenceCount, -1) == 0){\
		DELETE(X);\
	}\
}while(0)
