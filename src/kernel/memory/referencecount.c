#include"referencecount.h"
#include"kernel.h"

void initReferenceCount(ReferenceCount *rc, int value){
	rc->lock = initialSpinlock;
	rc->value = value;
}

int addReference(ReferenceCount *rc, int changeValue){
	acquireLock(&rc->lock);
	rc->value += changeValue;
	int newValue = rc->value;
	releaseLock(&rc->lock);
	return newValue;
}
