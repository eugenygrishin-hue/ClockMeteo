#ifndef APP_IR_H
#define APP_IR_H

#include <stdint.h>
#include <stdbool.h>
#include "ir_nec.h"

void APP_IR_Init(void);
void APP_IR_Process(void);

// Получить новую команду (не повтор). Возвращает true, если есть новая команда.
bool APP_IR_GetCommand(uint16_t *addr, uint8_t *cmd);
bool APP_IR_PeekCommand(uint16_t *addr, uint8_t *cmd);
void APP_IR_PushBack(uint16_t addr, uint8_t cmd);

// Проверить, был ли обнаружен повтор (удержание) после последнего вызова.
bool APP_IR_GetRepeat(void);

// Геттеры для последних успешных данных (для отображения на дисплее)
uint16_t APP_IR_GetLastAddress(void);
uint8_t  APP_IR_GetLastCommand(void);

// Глобальный декодер (для использования в обработчике прерывания)
extern IR_NEC_Decoder ir_decoder;

#endif
