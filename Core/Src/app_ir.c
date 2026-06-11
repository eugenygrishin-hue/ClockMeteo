#include "app_ir.h"
#include "main.h"
#include "ir_nec.h"
#include "ir_config.h"
#include "state_machine.h"
#include "millis.h"

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim9;

IR_NEC_Decoder ir_decoder;

// Флаги для обработки команд
static bool new_command_pending = false;
static bool repeat_pending = false;
static uint16_t pending_address = 0;
static uint8_t pending_command = 0;

static uint16_t last_address = 0;
static uint8_t last_command = 0;
static uint8_t last_was_repeat = 0;
static uint8_t last_is_repeat = 0;

void APP_IR_Init(void) {
    IR_NEC_InitDecoder(&ir_decoder, &huart1);
#ifdef IR_DEBUG
    IR_DebugPrint(&ir_decoder, "IR Receiver started\r\n");
#endif
    HAL_TIM_IC_Start_IT(&htim9, TIM_CHANNEL_1);
    IR_NEC_ResetDecoder(&ir_decoder);
    new_command_pending = false;
    repeat_pending = false;
}

void APP_IR_Process(void) {
    if (!ir_decoder.ready_flag) return;

    IR_NEC_Result result = IR_NEC_Decode(&ir_decoder);

    IR_DebugPrint(&ir_decoder, "IR: addr=0x%04X, cmd=0x%02X, status=%d, repeat=%d, raw=%d\n",
                  result.address, result.command, result.status, result.is_repeat, result.raw_data);

    if (result.status == IR_STATUS_OK) {
        if (result.is_repeat) {
            repeat_pending = true;
        } else {
            // новое нажатие – сохраняем в pending_*
            pending_address = result.address;
            pending_command = result.command;
            new_command_pending = true;
            // также сохраняем в last_* для геттеров
            last_address = result.address;
            last_command = result.command;
            last_was_repeat = false;
            last_is_repeat = false;
        }
    }
    IR_NEC_ResetDecoder(&ir_decoder);
}

bool APP_IR_GetCommand(uint16_t *addr, uint8_t *cmd) {
    if (new_command_pending) {
        *addr = pending_address;
        *cmd = pending_command;
        new_command_pending = false;
        return true;
    }
    return false;
}

bool APP_IR_PeekCommand(uint16_t *addr, uint8_t *cmd) {
	return new_command_pending ? true : false;
}

void APP_IR_PushBack(uint16_t addr, uint8_t cmd) {
    pending_address = addr;
    pending_command = cmd;
    new_command_pending = true;
}

bool APP_IR_GetRepeat(void) {
    if (repeat_pending) {
        repeat_pending = false;
        return true;
    }
    return false;
}

uint16_t APP_IR_GetLastAddress(void) {
    return pending_address;
}

uint8_t APP_IR_GetLastCommand(void) {
    return pending_command;
}
