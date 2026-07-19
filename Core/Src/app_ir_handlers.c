#include "state_machine.h"
#include "app_ir.h"
#include "tef6686.h"

static bool IR_GlobalHandler(IR_Event_t *ev) {
    switch(ev->command) {
        case 0x0A: // AUDIO button
            if (StateMachine_GetState() != STATE_RADIO) {
                StateMachine_SetState(STATE_RADIO);
                return true;
            }
            return false;
        case 0x01: // POWER button
            if (StateMachine_GetState() != STATE_MAIN) {
                StateMachine_SetState(STATE_MAIN);
                return true;
            }
            return false;
        default:
            return false;
    }
}

void IR_ProcessEvents(void) {
    IR_Event_t ev;
    while (IR_Event_Queue_Pop(&ev)) {
        if (!IR_GlobalHandler(&ev)) {
            // Если событие не обработано глобально, передаём его локальному обработчику.
            // Сохраняем команду в старые переменные, чтобы существующие обработчики состояний (on_main_process и др.)
            // могли её получить через APP_IR_GetCommand.
            //APP_IR_PushBack(ev.address, ev.command);
        }
    }
}
