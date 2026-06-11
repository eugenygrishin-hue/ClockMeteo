#ifndef VFD_DRIVER_H
#define VFD_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

#define NUM_DIGITS       12
#define NUM_PICT_GROUPS  4

#define NUM_POSITIONS 16 // 12 знаков + 4 группы пиктограмм

// Теневой буфер готовых пакетов для отправки по SPI
static uint8_t vfd_tx_buffer[NUM_POSITIONS][8];

// Прототипы функций
void Display_RenderBuffer(void);
void Display_Update(void);


extern SPI_HandleTypeDef hspi1;

void Display_Init(void);
void Display_PutChar(uint8_t pos, uint8_t ch);
void Display_PrintString(uint8_t start_pos, char *str);
void Display_Update(void);
void set_pictogram(uint8_t group, uint8_t pict_num);
void clear_pictogram(uint8_t group, uint8_t pict_num);
void static_display(uint64_t mask);
void Display_ArrowUp(uint8_t pos);
void Display_ArrowDown(uint8_t pos);
void Display_SetBrightness(uint8_t pos, uint8_t brightness);
void Display_SetBlink(uint8_t pos, uint8_t blink);
void Display_ShiftRows(uint8_t pos, int8_t direction);

// Новые функции для пиктограмм
void Display_SetPictBrightness(uint8_t group, uint8_t brightness);
void Display_SetPictBlink(uint8_t group, uint8_t pict_num, uint8_t blink);

#endif
