#include "app_ir_event.h"

static IR_Event_t queue[IR_EVENT_QUEUE_SIZE];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;

void IR_Event_Queue_Init(void) {
    head = 0;
    tail = 0;
}

bool IR_Event_Queue_Push(IR_Event_t *ev) {
    uint8_t next = (head + 1) % IR_EVENT_QUEUE_SIZE;
    if (next == tail) {
        return false; // Queue is full
    }
    queue[head] = *ev;
    head = next;
    return true;
}

bool IR_Event_Queue_Pop(IR_Event_t *ev) {
    if (head == tail) {
        return false; // Queue is empty
    }
    *ev = queue[tail];
    tail = (tail + 1) % IR_EVENT_QUEUE_SIZE;
    return true;
}

bool IR_Event_Queue_IsEmpty(void) {
    return (head == tail);
}
