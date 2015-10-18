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

Task *currentTask(TaskManager *tm);
LinearMemoryManager *getTaskLinearMemory(Task *t);

void resume(/*TaskManager *tm, */Task *t);

TaskManager *createTaskManager(SegmentTable *gdt);
void initTaskManagement(SystemCallTable *systemCallTable);

#define V8086_STACK_TOP (0x7000)
#define V8086_STACK_BOTTOM (0x1000)
int switchToVirtual8086Mode(void (*cs_ip)(void));

// initial state is suspended
// task.c
Task *createUserTaskWithoutLoader(void (*eip0)(void), int priority);
// elfloader.c
Task *createUserTaskFromELF(int fileService, const char*fileName, uintptr_t nameLength, int priority);
// apply a custom loader to a task
Task *createUserTask(void (*loader)(void*), void *arg, size_t argSize, int priority);
// the loader function is responsible to initialize LinearBlockManager
typedef struct LinearMemoryBlockManager LinearMemoryBlockManager;
int initUserLinearBlockManager(uintptr_t beginAddr, uintptr_t initEndAddr);

// return UINTPTR_NULL if failed
// return task id if succeeded
// task id is an address in kernel space. we haven't defined the usage yet
uintptr_t systemCall_createThread(void(*entry)(void));
// always succeed and do not return
void terminateCurrentTask(void);
void systemCall_terminate(void);

void setTaskSystemCall(Task *t, SystemCallFunction f, uintptr_t a);
