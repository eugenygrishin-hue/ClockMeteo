/**
  * @file  bme280.h
  * @brief Заголовочный файл библиотеки BME280
  */
#ifndef BME280_H
#define BME280_H

#include "stm32f4xx_hal.h"  // или ваш заголовочный файл с HAL
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// Адрес BME280 (по умолчанию 0x76, если SDO на GND; если 0x77 – SDO на VDDIO)
#define BME280_ADDRESS          (0x76 << 1)   // для HAL используется 8-битный адрес со сдвигом

// Регистры BME280
#define BME280_REG_ID           0xD0
#define BME280_REG_RESET        0xE0
#define BME280_REG_STATUS       0xF3
#define BME280_REG_CTRL_HUM     0xF2
#define BME280_REG_CTRL_MEAS    0xF4
#define BME280_REG_CONFIG       0xF5
#define BME280_REG_PRESS_MSB    0xF7
#define BME280_REG_TEMP_MSB     0xFA
#define BME280_REG_HUM_MSB      0xFD

// Калибровочные регистры
#define BME280_REG_DIG_T1        0x88
#define BME280_REG_DIG_T2        0x8A
#define BME280_REG_DIG_T3        0x8C
#define BME280_REG_DIG_P1        0x8E
#define BME280_REG_DIG_P2        0x90
#define BME280_REG_DIG_P3        0x92
#define BME280_REG_DIG_P4        0x94
#define BME280_REG_DIG_P5        0x96
#define BME280_REG_DIG_P6        0x98
#define BME280_REG_DIG_P7        0x9A
#define BME280_REG_DIG_P8        0x9C
#define BME280_REG_DIG_P9        0x9E
#define BME280_REG_DIG_H1        0xA1
#define BME280_REG_DIG_H2        0xE1
#define BME280_REG_DIG_H3        0xE3
#define BME280_REG_DIG_H4        0xE4
#define BME280_REG_DIG_H5        0xE5   // занимает 3 байта: 0xE4, 0xE5, 0xE6
#define BME280_REG_DIG_H6        0xE7

#define BME280_ID               0x60    // ожидаемый ID сенсора

// Настройки oversampling и режимов
#define BME280_OSRS_H_OFF        0x00
#define BME280_OSRS_H_x1         0x01
#define BME280_OSRS_H_x2         0x02
#define BME280_OSRS_H_x4         0x03
#define BME280_OSRS_H_x8         0x04
#define BME280_OSRS_H_x16        0x05

#define BME280_OSRS_T_OFF        0x00
#define BME280_OSRS_T_x1         0x01
#define BME280_OSRS_T_x2         0x02
#define BME280_OSRS_T_x4         0x03
#define BME280_OSRS_T_x8         0x04
#define BME280_OSRS_T_x16        0x05

#define BME280_OSRS_P_OFF        0x00
#define BME280_OSRS_P_x1         0x01
#define BME280_OSRS_P_x2         0x02
#define BME280_OSRS_P_x4         0x03
#define BME280_OSRS_P_x8         0x04
#define BME280_OSRS_P_x16        0x05

#define BME280_MODE_SLEEP        0x00
#define BME280_MODE_FORCED       0x01
#define BME280_MODE_NORMAL       0x03

#define BME280_STBY_0_5          0x00
#define BME280_STBY_62_5         0x01
#define BME280_STBY_125          0x02
#define BME280_STBY_250          0x03
#define BME280_STBY_500          0x04
#define BME280_STBY_1000         0x05
#define BME280_STBY_10           0x06
#define BME280_STBY_20           0x07

#define BME280_FILTER_OFF        0x00
#define BME280_FILTER_2          0x01
#define BME280_FILTER_4          0x02
#define BME280_FILTER_8          0x03
#define BME280_FILTER_16         0x04

// Структура для хранения калибровочных коэффициентов
typedef struct {
  uint16_t dig_T1;
  int16_t  dig_T2;
  int16_t  dig_T3;
  uint16_t dig_P1;
  int16_t  dig_P2;
  int16_t  dig_P3;
  int16_t  dig_P4;
  int16_t  dig_P5;
  int16_t  dig_P6;
  int16_t  dig_P7;
  int16_t  dig_P8;
  int16_t  dig_P9;
  uint8_t  dig_H1;
  int16_t  dig_H2;
  uint8_t  dig_H3;
  int16_t  dig_H4;
  int16_t  dig_H5;
  int8_t   dig_H6;
} BME280_CalibData;

// Публичные функции
void     BME280_Init(I2C_HandleTypeDef *hi2c);
float    BME280_ReadTemperature(void);
float    BME280_ReadPressure(void);
float    BME280_ReadHumidity(void);
float    BME280_ReadAltitude(float seaLevel_hPa);
void     BME280_SetOversamplingHum(uint8_t osrs);
void     BME280_SetOversamplingTemp(uint8_t osrs);
void     BME280_SetOversamplingPress(uint8_t osrs);
void     BME280_SetMode(uint8_t mode);
void     BME280_SetStandby(uint8_t tsb);
void     BME280_SetFilter(uint8_t filter);

#endif // BME280_H
