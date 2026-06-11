#ifndef SENSOR_DS3231_H
#define SENSOR_DS3231_H

#include "sensor.h"
#include "ds3232.h"          // Используем готовую библиотеку
#include "stm32f4xx_hal.h"

/**
 * @brief Создаёт структуру датчика DS3231.
 * @param hi2c Указатель на I2C (не используется, если библиотека жёстко привязана к hi2c2)
 * @param poll_interval_ms Период опроса в мс (например, 1000)
 * @return Указатель на Sensor_t
 */
Sensor_t* DS3231_CreateSensor(I2C_HandleTypeDef* hi2c, uint32_t poll_interval_ms);

#endif
