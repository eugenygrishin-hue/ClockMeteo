#ifndef VEML7700_H
#define VEML7700_H

#include "stm32f4xx_hal.h"

// Адрес VEML7700 (7-битный)
#define VEML7700_ADDR 0x10

// Регистры
#define VEML7700_REG_ALS_CONF       0x00
#define VEML7700_REG_ALS_WH          0x01
#define VEML7700_REG_ALS_WL          0x02
#define VEML7700_REG_POWER_SAVING    0x03
#define VEML7700_REG_ALS              0x04
#define VEML7700_REG_WHITE            0x05
#define VEML7700_REG_INTERRUPT        0x06

// Биты конфигурации
#define VEML7700_CONF_GAIN_MASK      0x1800  // биты 12-11
#define VEML7700_CONF_GAIN_1         0x0000  // x1
#define VEML7700_CONF_GAIN_2         0x0800  // x2
#define VEML7700_CONF_GAIN_1_8       0x1000  // 1/8
#define VEML7700_CONF_GAIN_1_4       0x1800  // 1/4

#define VEML7700_CONF_IT_MASK        0x00C0  // биты 7-6
#define VEML7700_CONF_IT_25          0x0000  // 25 ms
#define VEML7700_CONF_IT_50          0x0040  // 50 ms
#define VEML7700_CONF_IT_100         0x0080  // 100 ms
#define VEML7700_CONF_IT_200         0x00C0  // 200 ms

#define VEML7700_CONF_PERS_MASK      0x0030  // биты 5-4
#define VEML7700_CONF_INT_EN          0x0002  // interrupt enable
#define VEML7700_CONF_SD               0x0001  // shutdown

#define VEML7700_CONF_ALS_RDY         0x4000  // data ready (бит 14)

// Инициализация: сохраняет указатель на I2C, проверяет связь чтением конфигурации
HAL_StatusTypeDef VEML7700_Init(I2C_HandleTypeDef *hi2c);

// Чтение 16-битного регистра
HAL_StatusTypeDef VEML7700_ReadReg(uint8_t reg, uint16_t *data);

// Запись 16-битного регистра
HAL_StatusTypeDef VEML7700_WriteReg(uint8_t reg, uint16_t data);

// Чтение ALS (сырые данные)
HAL_StatusTypeDef VEML7700_ReadALS(uint16_t *als);

// Чтение белого канала (опционально)
HAL_StatusTypeDef VEML7700_ReadWhite(uint16_t *white);

// Получить текущие настройки усиления и времени интеграции
HAL_StatusTypeDef VEML7700_GetGainAndIT(uint8_t *gain, uint8_t *it);

// Рассчитать освещённость в люкс на основе сырых ALS и текущих настроек
float VEML7700_CalculateLux(uint16_t als, uint8_t gain, uint8_t it);

// Удобная функция: читает ALS, получает настройки и возвращает Lux
HAL_StatusTypeDef VEML7700_ReadLux(float *lux);

#endif
