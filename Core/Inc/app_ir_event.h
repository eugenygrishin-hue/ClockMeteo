#ifndef APP_IR_EVENT_H
#define APP_IR_EVENT_H

#include <stdint.h>
#include <stdbool.h>

#define IR_EVENT_QUEUE_SIZE 16

typedef struct {
    uint16_t address;
    uint8_t  command;
    bool     is_repeat;
} IR_Event_t;

void IR_Event_Queue_Init(void);
bool IR_Event_Queue_Push(IR_Event_t *ev);
bool IR_Event_Queue_Pop(IR_Event_t *ev);
bool IR_Event_Queue_IsEmpty(void);

#endif /* APP_IR_EVENT_H */
