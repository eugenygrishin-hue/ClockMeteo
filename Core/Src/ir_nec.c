#include "ir_nec.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ========== Инициализация и сброс ==========
void IR_NEC_InitDecoder(IR_NEC_Decoder* decoder, UART_HandleTypeDef* uart) {
    memset(decoder, 0, sizeof(IR_NEC_Decoder));
    decoder->receiving = false;
    decoder->ready_flag = false;
    decoder->result.status = IR_STATUS_IDLE;
    decoder->stop_interval = 0;
    decoder->new_command_flag = 0;
    decoder->repeat_flag = 0;
#ifdef IR_DEBUG
    decoder->debug_uart = uart;
#else
    (void)uart;
#endif
}

void IR_NEC_ResetDecoder(IR_NEC_Decoder* decoder) {
    decoder->receiving = false;
    decoder->ready_flag = false;
    decoder->pulse_count = 0;
    decoder->last_edge_time = 0;
    decoder->stop_interval = 0;
    decoder->result.status = IR_STATUS_IDLE;
    decoder->result.is_repeat = false;
    decoder->result.raw_data = false;
    // Не сбрасываем флаги и последние данные, так как они могут понадобиться
    // new_command_flag и repeat_flag будут сброшены при получении новых команд
}

// ========== Обработчик фронтов ==========
void IR_NEC_ProcessEdge(IR_NEC_Decoder* decoder, bool is_rising_edge, uint32_t timestamp) {
    if (decoder->ready_flag) return;

    uint32_t duration;
    if (timestamp >= decoder->last_edge_time) {
        duration = timestamp - decoder->last_edge_time;
    } else {
        duration = (0xFFFF - decoder->last_edge_time) + timestamp;
    }
    decoder->last_edge_time = timestamp;

    if (!decoder->receiving) {
        if (is_rising_edge && duration >= NEC_LEADER_LOW_MIN && duration <= NEC_LEADER_LOW_MAX) {
            decoder->receiving = true;
            decoder->pulse_count = 0;
            decoder->pulses[decoder->pulse_count++] = duration;
        }
        return;
    }

    if (decoder->pulse_count >= MAX_PULSES) {
        decoder->receiving = false;
        return;
    }

    decoder->pulses[decoder->pulse_count] = duration | (is_rising_edge ? 0x80000000 : 0);
    decoder->pulse_count++;

    if (!is_rising_edge && duration >= NEC_PACKET_END_MIN) {
        decoder->receiving = false;
        decoder->stop_interval = decoder->pulse_count - 1;
        decoder->ready_flag = true;
    }
}

// ========== Вспомогательные функции ==========
static bool is_valid_leader(const IR_NEC_Decoder* decoder) {
    if (decoder->pulse_count < 2) return false;
    uint32_t leader_low  = decoder->pulses[0] & 0x7FFFFFFF;
    uint32_t leader_high = decoder->pulses[1] & 0x7FFFFFFF;
    return (leader_low  >= NEC_LEADER_LOW_MIN)  && (leader_low  <= NEC_LEADER_LOW_MAX) &&
           (leader_high >= NEC_LEADER_HIGH_MIN) && (leader_high <= NEC_LEADER_HIGH_MAX);
}

static bool is_valid_nec_standard(uint16_t address, uint16_t command) {
    return ((address >> 8) == (uint8_t)~(address & 0xFF)) &&
           ((command >> 8) == (uint8_t)~(command & 0xFF));
}

static bool is_valid_nec_extended(uint16_t address, uint16_t command) {
    (void)address;
    return ((command >> 8) == (uint8_t)~(command & 0xFF));
}


// ========== Основная функция декодирования ==========
IR_NEC_Result IR_NEC_Decode(IR_NEC_Decoder* decoder) {
    IR_NEC_Result result = {0, 0, IR_STATUS_ERROR, false, false};

    // Проверка повторного пакета (2–4 импульса)
    if (decoder->pulse_count >= 2 && decoder->pulse_count <= 4) {
        if (is_valid_leader(decoder)) {
            uint16_t last_idx = decoder->pulse_count - 1;
            uint32_t last_duration = decoder->pulses[last_idx] & 0x7FFFFFFF;
            bool last_is_falling = !(decoder->pulses[last_idx] & 0x80000000);
            if (last_is_falling && last_duration >= NEC_PACKET_END_MIN) {
                result.is_repeat = true;
                result.status = IR_STATUS_OK;
                // Сохраняем последние успешные данные, если они есть
                result.address = decoder->last_address;
                result.command = decoder->last_command;
                decoder->repeat_flag = 1;   // устанавливаем флаг повтора
                decoder->ready_flag = false;
                return result;
            }
        }
    }

    // Полноценный пакет (32 бита данных)
    if (decoder->pulse_count < 66 || !is_valid_leader(decoder)) {
        result.status = IR_STATUS_ERROR;
        return result;
    }

    uint16_t address = 0;
    uint16_t command = 0;
    uint8_t bits_decoded = 0;

    for (uint16_t i = 2; i + 1 < decoder->stop_interval && bits_decoded < 32; i += 2) {
        if ((decoder->pulses[i] & 0x80000000) && !(decoder->pulses[i+1] & 0x80000000)) {
            uint32_t high_duration = decoder->pulses[i+1] & 0x7FFFFFFF;
            uint8_t bit_value = (high_duration > NEC_BIT_1_MIN) ? 1 : 0;
            if (bits_decoded < 16) {
                address |= (bit_value << (15 - bits_decoded));
            } else {
                command |= (bit_value << (15 - (bits_decoded - 16)));
            }
            bits_decoded++;
        }
    }

    uint32_t stop_duration = decoder->pulses[decoder->stop_interval] & 0x7FFFFFFF;
    bool stop_ok = !(decoder->pulses[decoder->stop_interval] & 0x80000000) &&
                   (stop_duration >= NEC_PACKET_END_MIN);
    result.raw_data = !stop_ok;
    result.is_repeat = false;

    // Определение протокола
    if (is_valid_nec_standard(address, command)) {
        result.address = (uint8_t)(address >> 8);
        result.command = (uint8_t)(command >> 8);
        result.status = IR_STATUS_OK;
    } else if (is_valid_nec_extended(address, command)) {
        result.address = address;
        result.command = (uint8_t)(command >> 8);
        result.status = IR_STATUS_OK;
    } else {
        result.address = address;
        result.command = command;
        result.status = IR_STATUS_ERROR;
        result.raw_data = true;
    }

    if (result.status == IR_STATUS_OK) {
        // Сохраняем последние успешные данные
        decoder->last_address = result.address;
        decoder->last_command = result.command;
        // Устанавливаем флаг новой команды (не повтор)
        decoder->new_command_flag = 1;
        decoder->repeat_flag = 0;
    }

    decoder->ready_flag = false;
    return result;
}

// ========== Отладочные функции ==========
void IR_DebugPrint(IR_NEC_Decoder* decoder, const char* format, ...) {
#ifdef IR_DEBUG
    if (!decoder->debug_uart) return;
    char buffer[128];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (len > 0) {
        HAL_UART_Transmit(decoder->debug_uart, (uint8_t*)buffer, len, HAL_MAX_DELAY);
    }
#else
    (void)decoder;
    (void)format;
#endif
}

void IR_DebugPrintPulses(IR_NEC_Decoder* decoder) {
#ifdef IR_DEBUG
    IR_DebugPrint(decoder, "IR: %d pulses captured:\r\n", decoder->pulse_count);
    for (int i = 0; i < decoder->pulse_count; i++) {
        uint32_t dur = decoder->pulses[i] & 0x7FFFFFFF;
        bool rising = (decoder->pulses[i] & 0x80000000) != 0;
        IR_DebugPrint(decoder, "%2d: %6lu us %s\r\n", i, dur, rising ? "RISING" : "FALLING");
    }
#else
    (void)decoder;
#endif
}
