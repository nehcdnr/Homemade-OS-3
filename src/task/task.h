#include<std.h>
#include"interrupt/systemcall.h"
#include"interrupt/handler.h"

typedef struct SystemCallTable SystemCallTable;
typedef struct SegmentTable SegmentTable;
typedef struct TaskManager TaskManager;
typedef struct Task Task;
typedef struct PageManager PageManager;
typedef struct LinearMemoryManager LinearMemoryManager;

// assume interrupt disabled
void schedule(void);

// put Task t into queue if it is suspended (by systemCall(SYSCALL_SUSPEND))
Task *currentTask(TaskManager *tm);
LinearMemoryManager *getTaskLinearMemory(Task *t);

void resume(/*TaskManager *tm, */Task *t);

TaskManager *createTaskManager(SegmentTable *gdt);
void initTaskManagement(SystemCallTable *systemCallTable);

// void defaultExitTask(void);

#define V8086_STACK_TOP (0x7000)
#define V8086_STACK_BOTTOM (0x1000)
int switchToVirtual8086Mode(void (*cs_ip)(void));

// initial state is suspended
//struct PageManager;
//struct MemoryBlockManager;
//Task *createKernelTask(void (*eip0)(void), int priority,
//	struct PageManager* page, struct MemoryBlockManager *linear);
Task *createUserTask(void (*eip)(void), int priority);

// return UINTPTR_NULL if failed
// return task id if succeeded
// task id is an address in kernel space. we haven't defined the usage yet
uintptr_t systemCall_createThread(void(*entry)(void));
// always succeed and do not return
void terminateCurrentTask(void);
void systemCall_terminate(void);

void setTaskSystemCall(Task *t, SystemCallFunction f, uintptr_t a);
