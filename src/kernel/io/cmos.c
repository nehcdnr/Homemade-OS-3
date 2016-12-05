#include"kernel.h"
#include"ioservice.h"
#include"multiprocessor/spinlock.h"
#include"multiprocessor/processorlocal.h"
#include"interrupt/controller/pic.h"
#include"interrupt/systemcalltable.h"

typedef enum{
	CMOS_SECOND = 0x00,
	CMOS_MINUTE = 0x02,
	CMOS_HOUR = 0x04,
	CMOS_WEEKDAY = 0x06,
	CMOS_DAY = 0x07,
	CMOS_MONTH = 0x08,
	CMOS_YEAR = 0x09,
	//CMOS_CENTURY = 0x32,
	CMOS_STATUS_A = 0x0a,
	CMOS_STATUS_B = 0x0b,
	CMOS_STATUS_C = 0x0c
}CMOSRegister;

typedef enum{
	CMOS_B_24_HOUR_MODE = (1 << 1),
	CMOS_B_BINARY_MODE = (1 << 2),
	CMOS_B_UPDATE_END_INTERRUPT = (1 << 4)
}CMOSStatusB;

typedef enum{
	CMOS_C_UPDATE_END_INTERRUPT = (1 << 4)
}CMOSStatusC;

static uint8_t readCMOS(CMOSRegister cmosNumber){
	out8(0x70, 0x80/*disable NMI*/ | cmosNumber);
	pause();
	return in8(0x71);
}

static void writeCMOS(CMOSRegister cmosNumber, uint8_t value){
	out8(0x70, 0x80/*disable NMI*/ | cmosNumber);
	pause();
	out8(0x71, value);
}

static const int daysInMonth[12] = {
	31, 28, 31, 30,
	31, 30, 31, 31,
	30, 31, 30, 31
};
// y>=1, 12>=m>=1, 31>=d>=1
// return 0 if y=m=d=1
static int countDays(int y, int m, int d){
	int r = (y - 1) * 365;
	r += (y - 1) / 4;
	r -= (y - 1) / 100;
	r += (y - 1) / 400;
	int i;
	for(i = 1; i < m; i++){
		r += daysInMonth[i - 1];
	}
	// y is leap year
	if(m > 2 && (y % 400 == 0 || (y % 100 != 0 && y % 4 == 0))){
		r++;
	}
	r += (d - 1);
	return r;
}

typedef struct{
	uint8_t year, month, day, hour, minute, second;
}DateTime;

typedef struct{
	uint8_t registerB;
	DateTime dateTime;
	uint64_t secondCount;
	Spinlock lock;
}CMOSStatus;

static uint64_t dateTimeToUint64(const DateTime *dt){
	// assume the year is 20xx
	uint64_t r = countDays(dt->year + 2000, dt->month, dt->day);
	r = r * 24 + dt->hour;
	r = r * 60 + dt->minute;
	r = r * 60 + dt->second;
	return r;
}

// return number of seconds since 0001/01/01
static void readCMOSTime(DateTime *dt, int binaryMode, int hour24Mode){
	dt->second = readCMOS(CMOS_SECOND);
	dt->minute = readCMOS(CMOS_MINUTE);
	dt->hour = readCMOS(CMOS_HOUR);
	int isPM = ((dt->hour >> 7) & 1);
	dt->hour &= 0x7f;
	dt->day = readCMOS(CMOS_DAY);
	dt->month = readCMOS(CMOS_MONTH);
	dt->year = readCMOS(CMOS_YEAR);
	// bcd to binary
	if(binaryMode == 0){
		uint8_t *t[6] = {&dt->second, &dt->minute, &dt->hour, &dt->day, &dt->month, &dt->year};
		unsigned i;
		for(i = 0; i < LENGTH_OF(t); i++){
			uint8_t *v = t[i];
			(*v) = ((*v) & 0xf) + ((*v) >> 4) * 10;
		}
	}
	// hour
	if(hour24Mode == 0){
		if(dt->hour == 12){
			dt->hour = 0;
		}
		if(isPM){
			dt->hour += 12;
		}
	}
}

static int setCMOSTimeHandler(const InterruptParam *p){
	CMOSStatus *s = (CMOSStatus*)p->argument;
	DateTime dt;
	readCMOSTime(&dt,
		(s->registerB & CMOS_B_BINARY_MODE) != 0,
		(s->registerB & CMOS_B_24_HOUR_MODE) != 0
	); // assume status register does not change
	uint64_t sec = dateTimeToUint64(&dt);
	acquireLock(&s->lock);
	s->dateTime = dt;
	s->secondCount = sec;
	releaseLock(&s->lock);
	// read C to enable next interrupt
	uint8_t c = readCMOS(CMOS_STATUS_C);
	if((c & CMOS_C_UPDATE_END_INTERRUPT)== 0){
		printk("unexpected realtime clock interrupt; status = %x", c);
	}
	//printk("%lld\n",sec);
	return 1;
}

static void getCMOSTimeHandler(InterruptParam *p){
	CMOSStatus *s = (CMOSStatus*)p->argument;
	acquireLock(&s->lock);
	uint64_t r = s->secondCount;
	releaseLock(&s->lock);
	SYSTEM_CALL_RETURN_VALUE_0(p) = LOW64(r);
	SYSTEM_CALL_RETURN_VALUE_1(p) = HIGH64(r);
}

static CMOSStatus cmosStatus;

void initCMOS(PIC *pic, SystemCallTable *s){
	// initialize CMOS
	cmosStatus.registerB = readCMOS(CMOS_STATUS_B);
	cmosStatus.registerB |= CMOS_B_UPDATE_END_INTERRUPT;
	DateTime *const dt = &cmosStatus.dateTime;
	readCMOSTime(dt,
		(cmosStatus.registerB & CMOS_B_BINARY_MODE) != 0,
		(cmosStatus.registerB & CMOS_B_24_HOUR_MODE) != 0
	);
	cmosStatus.secondCount = dateTimeToUint64(dt);
	cmosStatus.lock = initialSpinlock;

	printk("CMOS time is %u/%u/%u %u:%u:%u (%lld)\n",
		2000 + dt->year, dt->month, dt->day,
		dt->hour, dt->minute, dt->second,
		cmosStatus.secondCount);
	// interrupt to update time
	InterruptVector *v = pic->irqToVector(pic, REALTIME_CLOCK_IRQ);
	if(addHandler(v, setCMOSTimeHandler, (uintptr_t)&cmosStatus) == 0){
		panic("cannot initialize CMOS interrupt\n");
	}
	pic->setPICMask(pic, REALTIME_CLOCK_IRQ, 0);
	// enable CMOS update interrupt
	writeCMOS(CMOS_STATUS_B, cmosStatus.registerB);
	// system call to read time
	registerSystemCall(s, SYSCALL_GET_TIME, getCMOSTimeHandler, (uintptr_t)&cmosStatus);
}

#ifndef NDEBUG

void testCountDays(void);
void testCountDays(void){
	int a,b;
	// month & day
	a = countDays(1, 1, 1);
	assert(a == 0);
	a = countDays(1, 2, 1);
	assert(a == 31);
	a = countDays(1, 12, 31);
	assert(a == 364);
	// leap year
	a = countDays(4, 2, 1);
	b = countDays(4, 3, 1);
	assert(a + 29 == b);
	a = countDays(2000, 2, 1);
	b = countDays(2000, 3, 1);
	assert(a + 29 == b);
	a = countDays(2100, 2, 1);
	b = countDays(2100, 3, 1);
	assert(a + 28 == b);
	uint64_t prevSecond = systemCall_getTime();
	for(a = 0; a < 3; a++){
		sleep(1000);
		uint64_t currSecond = systemCall_getTime();
		printk("%lld\n", currSecond);
		//assert(now == lastSecond + 1);
		currSecond = prevSecond;
	}
	systemCall_terminate();
}

#endif
