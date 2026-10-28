#define NUM_TASKS 16
typedef void (*TaskFunc)(u8 taskId);
u8 CreateTask(TaskFunc fn, u8 priority);
void DestroyTask(u8 taskId);
u8 GetTaskCount(void);
