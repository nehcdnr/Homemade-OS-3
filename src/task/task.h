#include<std.h>
#include"interrupt/systemcall.h"
#include"interrupt/handler.h"

typedef struct SystemCallTable SystemCallTable;
typedef struct SegmentTable SegmentTable;
typedef struct TaskManager TaskManager;
typedef struct Task Task;

// pause current task and put it into queue
void schedule(TaskManager *tm);
// put Task t into queue if it is suspended (by systemCall(SYSCALL_SUSPEND))
void suspend(Task *t);
Task *currentTask(TaskManager *tm);
void resume(/*TaskManager *tm, */Task *t);

TaskManager *createTaskManager(SegmentTable *gdt);
void initTaskManagement(SystemCallTable *systemCallTable);

void defaultExitTask(void);

#define V8086_STACK_TOP (0x7000)
#define V8086_STACK_BOTTOM (0x1000)
int switchToVirtual8086Mode(void (*cs_ip)(void));

// initial state is suspended
Task *createKernelTask(void(*eip0)(void));

void setTaskSystemCall(Task *t, SystemCallFunction f, uintptr_t a);
