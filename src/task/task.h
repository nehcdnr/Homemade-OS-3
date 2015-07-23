#include<std.h>
#include"interrupt/systemcall.h"
#include"interrupt/handler.h"

typedef struct SystemCallTable SystemCallTable;
typedef struct SegmentTable SegmentTable;
typedef struct TaskManager TaskManager;
typedef struct Task Task;
typedef struct PageManager PageManager;
typedef struct LinearMemoryManager LinearMemoryManager;
void schedule(void);

// put Task t into queue if it is suspended (by systemCall(SYSCALL_SUSPEND))
Task *currentTask(TaskManager *tm);
PageManager *getTaskPageManager(Task *t);
LinearMemoryManager *getTaskLinearMemory(Task *t);

void resume(/*TaskManager *tm, */Task *t);


TaskManager *createTaskManager(SegmentTable *gdt);
void initTaskManagement(SystemCallTable *systemCallTable);

// void defaultExitTask(void);

#define V8086_STACK_TOP (0x7000)
#define V8086_STACK_BOTTOM (0x1000)
int switchToVirtual8086Mode(void (*cs_ip)(void));

// initial state is suspended
Task *createKernelTask(void(*eip0)(void), int priority);

void setTaskSystemCall(Task *t, SystemCallFunction f, uintptr_t a);
