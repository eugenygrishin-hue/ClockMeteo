#include "state_machine.h"
#include "millis.h"

static const StateDescriptor_t* state_table = NULL;
static SystemState_t current_state;
static uint32_t state_enter_time;

void StateMachine_Init(const StateDescriptor_t* states, SystemState_t initial) {
    state_table = states;
    current_state = initial;
    state_enter_time = Millis_Get();
    if (state_table[current_state].on_enter)
        state_table[current_state].on_enter();
}

void StateMachine_SetState(SystemState_t new_state) {
    if (new_state == current_state || new_state >= STATE_COUNT || state_table == NULL)
        return;
    if (state_table[current_state].on_exit)
        state_table[current_state].on_exit();
    current_state = new_state;

    // Внедряем менеджер пиктограмм здесь!
    VFD_Icons_ApplyProfile(new_state);

    state_enter_time = Millis_Get();
    if (state_table[current_state].on_enter)
        state_table[current_state].on_enter();
}

void StateMachine_Process(void) {
    if (state_table == NULL) return;
    uint32_t timeout = state_table[current_state].timeout_ms;
    if (timeout > 0 && (Millis_Get() - state_enter_time) >= timeout) {
        StateMachine_SetState(state_table[current_state].next_state);
    }
    if (state_table[current_state].process)
        state_table[current_state].process();
}

SystemState_t StateMachine_GetState(void) {
    return current_state;
}

bool StateMachine_IsState(SystemState_t state) {
    return current_state == state;
}

void StateMachine_ResetTimeout(void)
{
    if (state_table != NULL) {
        state_enter_time = Millis_Get();
    }
}
