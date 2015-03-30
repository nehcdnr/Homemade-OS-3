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
void resume(/*TaskManager *tm, */Task *t);

TaskManager *createTaskManager(SystemCallTable *systemCallTable, SegmentTable *gdt);

void defaultExitTask(void);

void startVirtual8086Task(void (*cs_ip)(void), uintptr_t ss_sp);

// initial state is suspended
Task *createKernelTask(void(*eip0)(void));
void setTaskSystemCall(Task *t, SystemCallFunction f);
