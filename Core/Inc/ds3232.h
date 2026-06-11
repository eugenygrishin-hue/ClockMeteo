#ifndef DS3232_H
#define DS3232_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

// Типы дней: неделя или дата
typedef enum {
    ALARM_DAY_DATE = 0,
    ALARM_DAY_WEEK = 1
} AlarmDayType;

// Структура для Alarm1 (с секундами)
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    AlarmDayType day_type;

    uint8_t mask_sec;
    uint8_t mask_min;
    uint8_t mask_hour;
    uint8_t mask_day;
} DS3232_Alarm1;

// Структура для Alarm2 (без секунд)
typedef struct {
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    AlarmDayType day_type;

    uint8_t mask_min;
    uint8_t mask_hour;
    uint8_t mask_day;
} DS3232_Alarm2;

// Структура времени
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t date;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
} DS3232_Time;

// Инициализация и базовые функции
void DS3232_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef DS3232_CheckDevice(void);
HAL_StatusTypeDef DS3232_GetTime(DS3232_Time *time);
HAL_StatusTypeDef DS3232_SetTime(DS3232_Time *time);

// SRAM
HAL_StatusTypeDef DS3232_ReadSRAM(uint8_t memAddr, uint8_t *pData, uint16_t len);
HAL_StatusTypeDef DS3232_WriteSRAM(uint8_t memAddr, uint8_t *pData, uint16_t len);

// Будильники
HAL_StatusTypeDef DS3232_GetAlarm1(DS3232_Alarm1 *alarm);
HAL_StatusTypeDef DS3232_SetAlarm1(const DS3232_Alarm1 *alarm);
HAL_StatusTypeDef DS3232_GetAlarm2(DS3232_Alarm2 *alarm);
HAL_StatusTypeDef DS3232_SetAlarm2(const DS3232_Alarm2 *alarm);
HAL_StatusTypeDef DS3232_EnableAlarm1(uint8_t enable);
HAL_StatusTypeDef DS3232_EnableAlarm2(uint8_t enable);
HAL_StatusTypeDef DS3232_ClearAlarm1Flag(void);
HAL_StatusTypeDef DS3232_ClearAlarm2Flag(void);
uint8_t calculate_weekday(uint16_t y, uint8_t m, uint8_t d);

#endif
