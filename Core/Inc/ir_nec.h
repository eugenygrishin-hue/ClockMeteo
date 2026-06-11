#ifndef IR_NEC_H
#define IR_NEC_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "ir_config.h"

// Константы протокола NEC (в микросекундах)
#define NEC_LEADER_LOW_MIN  8500
#define NEC_LEADER_LOW_MAX  9500
#define NEC_LEADER_HIGH_MIN 2000
#define NEC_LEADER_HIGH_MAX 5000
#define NEC_BIT_0_MAX       1000
#define NEC_BIT_1_MIN       1500
#define NEC_PACKET_END_MIN  20000
#define MAX_PULSES 180

typedef enum {
    IR_STATUS_IDLE,
    IR_STATUS_RECEIVING,
    IR_STATUS_OK,
    IR_STATUS_ERROR,
    IR_STATUS_READY
} IR_Status;

typedef struct {
    uint16_t address;
    uint16_t command;
    IR_Status status;
    bool raw_data;
    bool is_repeat;
} IR_NEC_Result;

typedef struct {
    uint32_t pulses[MAX_PULSES];
    uint16_t pulse_count;
    uint32_t last_edge_time;
    bool receiving;
    bool ready_flag;
    uint16_t stop_interval;
    IR_NEC_Result result;

    // Новые поля для обработки команд
    uint8_t new_command_flag;   // 1 – новая команда (не повтор)
    uint8_t repeat_flag;        // 1 – обнаружен повтор (удержание)
    uint16_t last_address;      // последний успешный адрес
    uint8_t last_command;       // последняя успешная команда

#ifdef IR_DEBUG
    UART_HandleTypeDef* debug_uart;
#endif
} IR_NEC_Decoder;

void IR_NEC_InitDecoder(IR_NEC_Decoder* decoder, UART_HandleTypeDef* uart);
void IR_NEC_ResetDecoder(IR_NEC_Decoder* decoder);
void IR_NEC_ProcessEdge(IR_NEC_Decoder* decoder, bool is_rising_edge, uint32_t timestamp);
IR_NEC_Result IR_NEC_Decode(IR_NEC_Decoder* decoder);
void IR_DebugPrint(IR_NEC_Decoder* decoder, const char* format, ...);
void IR_DebugPrintPulses(IR_NEC_Decoder* decoder);

#endif
