#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx_hal.h"

// Пины управления VFD
#define VFD_LE_PORT  GPIOA
#define VFD_LE_PIN   GPIO_PIN_0
#define VFD_STR_PORT GPIOA
#define VFD_STR_PIN  GPIO_PIN_1

// Пины светодиода
#define LED_PORT     GPIOC
#define LED_PIN      GPIO_PIN_13

#endif /* BOARD_H */
