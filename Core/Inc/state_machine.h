#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    STATE_MAIN = 0,
    STATE_SHOW_TEMP,
    STATE_SHOW_PRESSURE,
    STATE_SHOW_HUMMID,
    STATE_SHOW_DATE,
    STATE_SHOW_IR,
    STATE_EDIT_TIME,
    STATE_EDIT_DATE,
    STATE_RADIO,
	STATE_EDIT_ALARM,
	STATE_ALARM_SELECT_RADIO,
	STATE_ALARM_RINGING,
    STATE_COUNT
} SystemState_t;

typedef void (*StateFunc)(void);

typedef struct {
    StateFunc on_enter;
    StateFunc on_exit;
    StateFunc process;
    uint32_t timeout_ms;
    SystemState_t next_state;
} StateDescriptor_t;

void StateMachine_Init(const StateDescriptor_t* states, SystemState_t initial);
void StateMachine_SetState(SystemState_t new_state);
void StateMachine_Process(void);
SystemState_t StateMachine_GetState(void);
bool StateMachine_IsState(SystemState_t state);
void StateMachine_ResetTimeout(void);

#endif
