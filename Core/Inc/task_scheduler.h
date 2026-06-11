#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*TaskFunction_t)(void);

typedef struct {
    TaskFunction_t function;    // указатель на функцию задачи
    uint32_t interval_ms;       // период выполнения (мс)
    uint32_t last_execution;    // время последнего запуска
    bool enabled;               // задача активна?
    const char* name;           // имя задачи (для отладки)
} Task_t;

#define MAX_TASKS 15

// Инициализация планировщика
void TaskScheduler_Init(void);

// Добавление задачи
bool TaskScheduler_AddTask(TaskFunction_t func, uint32_t interval_ms, const char* name);

// Включение/отключение задачи по имени
void TaskScheduler_SetTaskEnabled(const char* name, bool enabled);

// Основная функция, вызываемая в цикле while(1)
void TaskScheduler_Run(void);

#endif
