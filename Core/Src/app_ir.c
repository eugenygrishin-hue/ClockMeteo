#include "app_ir.h"
#include "main.h"
#include "ir_nec.h"
#include "ir_config.h"
#include "state_machine.h"
#include "millis.h"

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim9;

IR_NEC_Decoder ir_decoder;

// Глобальная переменная для хранения последней команды
static volatile uint16_t pending_address = 0;
static volatile uint8_t pending_command = 0;
static volatile bool command_pending = false;

void APP_IR_Init(void) {
    IR_NEC_InitDecoder(&ir_decoder, &huart1);

#ifdef IR_DEBUG
    IR_DebugPrint(&ir_decoder, "IR Receiver started\r\n");
#endif
    HAL_TIM_IC_Start_IT(&htim9, TIM_CHANNEL_1);
    IR_NEC_ResetDecoder(&ir_decoder);
}

void APP_IR_Process(void) {
    if (!ir_decoder.ready_flag) return;

    IR_NEC_Result result = IR_NEC_Decode(&ir_decoder);

    if (result.address != 0 || result.command != 0) {
        // Логируем ТОЛЬКО когда команда реально пришла с пульта
        IR_DebugPrint(&ir_decoder, "[IR] Received: addr=0x%04X, cmd=0x%02X, rep=%d\n",
                      result.address, result.command, result.is_repeat);

        // Сохраняем команду в глобальную переменную
        __disable_irq();
        if (!result.is_repeat) {
            pending_address = result.address;
            pending_command = result.command;
            command_pending = true;
        }
        __enable_irq();
    }
    IR_NEC_ResetDecoder(&ir_decoder);
}

bool APP_IR_GetCommand(uint16_t *addr, uint8_t *cmd) {
    __disable_irq();
    if (command_pending) {
        *addr = pending_address;
        *cmd = pending_command;
        command_pending = false; // Сбрасываем флаг после чтения
        __enable_irq();
        return true;
    }
    __enable_irq();
    return false;
}

void APP_IR_PushBack(uint16_t addr, uint8_t cmd) {
    __disable_irq();
    pending_address = addr;
    pending_command = cmd;
    command_pending = true;
    __enable_irq();
}

bool APP_IR_PeekCommand(uint16_t *addr, uint8_t *cmd) {
    __disable_irq();
    bool has_cmd = command_pending;
    if (has_cmd) {
        *addr = pending_address;
        *cmd = pending_command;
    }
    __enable_irq();
    return has_cmd;
}

bool APP_IR_GetRepeat(void) { return false; }
uint16_t APP_IR_GetLastAddress(void) { return pending_address; }
uint8_t APP_IR_GetLastCommand(void) { return pending_command; }
