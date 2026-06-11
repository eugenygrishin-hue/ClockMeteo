#include "task_scheduler.h"
#include "millis.h"

#include <string.h>

static Task_t tasks[MAX_TASKS];
static uint8_t task_count = 0;

void TaskScheduler_Init(void) {
    for (uint8_t i = 0; i < MAX_TASKS; i++) {
        tasks[i].function = NULL;
        tasks[i].enabled = false;
    }
    task_count = 0;
}

bool TaskScheduler_AddTask(TaskFunction_t func, uint32_t interval_ms, const char* name) {
    if (task_count >= MAX_TASKS || func == NULL) return false;
    tasks[task_count].function = func;
    tasks[task_count].interval_ms = interval_ms;
    tasks[task_count].last_execution = Millis_Get(); // текущее время
    tasks[task_count].enabled = true;
    tasks[task_count].name = name;
    task_count++;
    return true;
}

void TaskScheduler_SetTaskEnabled(const char* name, bool enabled) {
    for (uint8_t i = 0; i < task_count; i++) {
        if (strcmp(tasks[i].name, name) == 0) {
            tasks[i].enabled = enabled;
            break;
        }
    }
}

void TaskScheduler_Run(void) {
    uint32_t now = Millis_Get();
    for (uint8_t i = 0; i < task_count; i++) {
        if (!tasks[i].enabled) continue;
        if ((now - tasks[i].last_execution) >= tasks[i].interval_ms) {
            tasks[i].function();          // выполняем задачу
            tasks[i].last_execution = now;
        }
    }
}
