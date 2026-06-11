#ifndef MILLIS_H
#define MILLIS_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/**
 * @brief Инициализация таймера для отсчёта миллисекунд.
 * @param htim Указатель на структуру таймера HAL (например, &htim2).
 *        Таймер должен быть предварительно настроен в CubeMX,
 *        но не запущен.
 */
void Millis_Init(TIM_HandleTypeDef *htim);

/**
 * @brief Возвращает текущее значение миллисекундного счётчика.
 * @return Количество миллисекунд с момента запуска таймера
 *         (переполняется через ~49 дней).
 */
uint32_t Millis_Get(void);

/**
 * @brief Обработчик прерывания таймера (должен вызываться из HAL_TIM_PeriodElapsedCallback).
 */
void Millis_Inc(void);

#endif
