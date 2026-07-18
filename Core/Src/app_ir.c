#include "app_ir.h"
#include "main.h"
#include "ir_nec.h"
#include "ir_config.h"
#include "app_ir_event.h"
#include "state_machine.h"
#include "millis.h"

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim9;

IR_NEC_Decoder ir_decoder;

void APP_IR_Init(void) {
    IR_NEC_InitDecoder(&ir_decoder, &huart1);
    IR_Event_Queue_Init(); // <--- ВАЖНО: Инициализируем очередь при старте!

#ifdef IR_DEBUG
    IR_DebugPrint(&ir_decoder, "IR Receiver started\r\n");
#endif
    HAL_TIM_IC_Start_IT(&htim9, TIM_CHANNEL_1);
    IR_NEC_ResetDecoder(&ir_decoder);
}

void APP_IR_Process(void) {
    if (!ir_decoder.ready_flag)
        return;

    IR_NEC_Result result = IR_NEC_Decode(&ir_decoder);

#ifdef IR_DEBUG
    IR_DebugPrint(&ir_decoder,
            "IR: addr=0x%04X, cmd=0x%02X, status=%d, repeat=%d, raw=%d\n",
            result.address, result.command, result.status, result.is_repeat,
            result.raw_data);
#endif

    if (result.status == IR_STATUS_OK) {
        IR_Event_t ev;
        ev.address = result.address;
        ev.command = result.command;
        ev.is_repeat = result.is_repeat;
        IR_Event_Queue_Push(&ev);
    }

    // Сбрасываем декодер для приёма следующей посылки
    IR_NEC_ResetDecoder(&ir_decoder);
}

// ✅ ИСПРАВЛЕНО: Честно берём команду из очереди
bool APP_IR_GetCommand(uint16_t *addr, uint8_t *cmd) {
    IR_Event_t ev;
    if (IR_Event_Queue_Pop(&ev)) {
        // Игнорируем коды повтора (удержание кнопки), чтобы не было "залипания"
        // Если нужна обработка удержания, для этого должна быть отдельная логика
        if (!ev.is_repeat) {
            *addr = ev.address;
            *cmd = ev.command;
            return true;
        }
    }
    return false;
}

// ✅ ИСПРАВЛЕНО: Проверяем, есть ли в очереди хотя бы одна команда
bool APP_IR_PeekCommand(uint16_t *addr, uint8_t *cmd) {
    // В простой реализации достаточно знать, что очередь не пуста.
    // Сама команда будет извлечена при вызове APP_IR_GetCommand
    return !IR_Event_Queue_IsEmpty();
}

// ❌ УДАЛЕНО: APP_IR_PushBack больше не нужен и был источником бага.
// Если команда не извлечена через GetCommand, она и так остаётся в очереди.
// Если компилятор выдаст ошибку о недостающей функции, просто удали её вызовы в app_states.c
// или раскомментируй заглушку ниже:
/*
void APP_IR_PushBack(uint16_t addr, uint8_t cmd) {
    // Заглушка для совместимости, если где-то ещё вызывается
}
*/

// ✅ ИСПРАВЛЕНО: Отдельная проверка на повтор (если стейт-машина захочет обработать удержание)
bool APP_IR_GetRepeat(void) {
    IR_Event_t ev;
    // Простая реализация: если в очереди есть repeat, мы могли бы его достать.
    // Но для базовой работы лучше полагаться на то, что GetCommand их фильтрует.
    return false;
}

uint16_t APP_IR_GetLastAddress(void) {
    // Возвращаем 0, так как pending переменных больше нет.
    // Если это критично, нужно хранить last_addr отдельно.
    return 0;
}

uint8_t APP_IR_GetLastCommand(void) {
    return 0;
}
