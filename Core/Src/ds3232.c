#include "ds3232.h"
#include <string.h>
#include <stdio.h>

// Адрес DS3232 на шине I2C (7-битный)
#define DS3232_I2C_ADDR    0x68

// Регистры времени
#define DS3232_REG_SEC     0x00
#define DS3232_REG_MIN     0x01
#define DS3232_REG_HOUR    0x02
#define DS3232_REG_DAY     0x03   // день недели (не используется)
#define DS3232_REG_DATE    0x04
#define DS3232_REG_MONTH   0x05
#define DS3232_REG_YEAR    0x06

// Начало SRAM
#define DS3232_SRAM_START  0x0E

// Статический указатель на I2C, устанавливаемый в DS3232_Init
static I2C_HandleTypeDef *ds3232_i2c = NULL;

// ======================= Вспомогательные функции BCD ↔ двоичный =======================
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static uint8_t bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

uint8_t calculate_weekday(uint16_t y, uint8_t m, uint8_t d) {
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

// ======================= Чтение/запись регистров =======================
static HAL_StatusTypeDef DS3232_ReadReg(uint8_t reg, uint8_t *data, uint16_t len) {
    if (ds3232_i2c == NULL) return HAL_ERROR;
    return HAL_I2C_Mem_Read(ds3232_i2c, DS3232_I2C_ADDR << 1, reg,
                            I2C_MEMADD_SIZE_8BIT, data, len, 100);
}

static HAL_StatusTypeDef DS3232_WriteReg(uint8_t reg, uint8_t *data, uint16_t len) {
    if (ds3232_i2c == NULL) return HAL_ERROR;
    return HAL_I2C_Mem_Write(ds3232_i2c, DS3232_I2C_ADDR << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, data, len, 100);
}

// ======================= Парсинг времени компиляции =======================
static void parse_compile_time(uint16_t *year, uint8_t *month, uint8_t *day,
                               uint8_t *hour, uint8_t *min, uint8_t *sec) {
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char mon[4] = {0};
    int y, d, h, m, s;
    sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
    *day = (uint8_t)d;
    *year = (uint16_t)y;
    for (int i = 0; i < 12; i++) {
        if (strncmp(mon, months[i], 3) == 0) {
            *month = i + 1;
            break;
        }
    }
    sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
    *hour = (uint8_t)h;
    *min = (uint8_t)m;
    *sec = (uint8_t)s;
}

// ======================= Инициализация и базовые функции =======================
void DS3232_Init(I2C_HandleTypeDef *hi2c) {
    ds3232_i2c = hi2c;

    // Проверяем наличие устройства
    if (HAL_I2C_IsDeviceReady(ds3232_i2c, DS3232_I2C_ADDR << 1, 3, 100) != HAL_OK) {
        return;
    }

    // Читаем регистр секунд для проверки OSF (бит 7)
    uint8_t sec_reg;
    if (DS3232_ReadReg(DS3232_REG_SEC, &sec_reg, 1) != HAL_OK) {
        return;
    }

}

HAL_StatusTypeDef DS3232_CheckDevice(void) {
    if (ds3232_i2c == NULL) return HAL_ERROR;
    return HAL_I2C_IsDeviceReady(ds3232_i2c, DS3232_I2C_ADDR << 1, 3, 100);
}

HAL_StatusTypeDef DS3232_GetTime(DS3232_Time *time) {
    uint8_t buf[7];
    HAL_StatusTypeDef status = DS3232_ReadReg(DS3232_REG_SEC, buf, 7);
    if (status != HAL_OK) return status;
    time->second = bcd_to_bin(buf[0] & 0x7F);
    time->minute = bcd_to_bin(buf[1] & 0x7F);
    time->hour   = bcd_to_bin(buf[2] & 0x3F);

    time->date   = bcd_to_bin(buf[4] & 0x3F);
    time->month  = bcd_to_bin(buf[5] & 0x1F);
    time->year   = 2000 + bcd_to_bin(buf[6]);

    time->weekday = calculate_weekday(time->year, time->month, time->date);
    return HAL_OK;
}

HAL_StatusTypeDef DS3232_SetTime(DS3232_Time *time) {
    uint8_t buf[7];

    // Рассчитываем день недели на основе даты и года
    // Используем полную дату: например, 2026, 5, 8
    time->weekday = calculate_weekday(time->year, time->month, time->date);

    buf[0] = bin_to_bcd(time->second) & 0x7F;
    buf[1] = bin_to_bcd(time->minute);
    buf[2] = bin_to_bcd(time->hour);

    // DS3231 ожидает 1-7. Если наш алгоритм выдал 0 (Вс), запишем 7,
    // чтобы не было нуля в регистре (некоторые чипы это не любят).
    // Но если твой массив weekdays[0] это "Sun", то можно оставить 0 или +1.
    // Давай сделаем 1=Mon...7=Sun для стандарта DS3231:
    uint8_t ds_weekday = (time->weekday == 0) ? 7 : time->weekday;
    buf[3] = ds_weekday;

    buf[4] = bin_to_bcd(time->date);
    buf[5] = bin_to_bcd(time->month);
    buf[6] = bin_to_bcd(time->year % 100);

    return DS3232_WriteReg(DS3232_REG_SEC, buf, 7);
}


// ======================= SRAM =======================
HAL_StatusTypeDef DS3232_ReadSRAM(uint8_t memAddr, uint8_t *pData, uint16_t len) {
    if (memAddr < DS3232_SRAM_START) return HAL_ERROR;
    if (ds3232_i2c == NULL) return HAL_ERROR;
    return HAL_I2C_Mem_Read(ds3232_i2c, DS3232_I2C_ADDR << 1, memAddr,
                            I2C_MEMADD_SIZE_8BIT, pData, len, 100);
}

HAL_StatusTypeDef DS3232_WriteSRAM(uint8_t memAddr, uint8_t *pData, uint16_t len) {
    if (memAddr < DS3232_SRAM_START) return HAL_ERROR;
    if (ds3232_i2c == NULL) return HAL_ERROR;
    return HAL_I2C_Mem_Write(ds3232_i2c, DS3232_I2C_ADDR << 1, memAddr,
                             I2C_MEMADD_SIZE_8BIT, pData, len, 100);
}

// ======================= Вспомогательные функции для будильников =======================
static uint8_t pack_alarm_value(uint8_t value, uint8_t mask) {
    if (mask) return 0x80 | (value & 0x7F);
    else return bin_to_bcd(value) & 0x7F;
}

static void unpack_alarm_value(uint8_t reg, uint8_t *value, uint8_t *mask) {
    *mask = (reg >> 7) & 1;
    *value = (*mask) ? (reg & 0x7F) : bcd_to_bin(reg & 0x7F);
}

// ======================= Alarm1 =======================
HAL_StatusTypeDef DS3232_GetAlarm1(DS3232_Alarm1 *alarm) {
    uint8_t buf[4]; // регистры 0x07..0x0A
    HAL_StatusTypeDef status = DS3232_ReadReg(0x07, buf, 4);
    if (status != HAL_OK) return status;

    unpack_alarm_value(buf[0], &alarm->second, &alarm->mask_sec);
    unpack_alarm_value(buf[1], &alarm->minute, &alarm->mask_min);
    unpack_alarm_value(buf[2], &alarm->hour,   &alarm->mask_hour);

    uint8_t day_reg = buf[3];
    alarm->mask_day = (day_reg >> 7) & 1;
    alarm->day_type = (day_reg >> 6) & 1;  // бит 6: 0 – дата, 1 – день недели
    alarm->day = (alarm->mask_day) ? (day_reg & 0x3F) : bcd_to_bin(day_reg & 0x3F);

    return HAL_OK;
}

HAL_StatusTypeDef DS3232_SetAlarm1(const DS3232_Alarm1 *alarm) {
    uint8_t buf[4];
    buf[0] = pack_alarm_value(alarm->second, alarm->mask_sec);
    buf[1] = pack_alarm_value(alarm->minute, alarm->mask_min);
    buf[2] = pack_alarm_value(alarm->hour,   alarm->mask_hour);

    uint8_t day_reg = pack_alarm_value(alarm->day, alarm->mask_day);
    if (alarm->day_type == ALARM_DAY_WEEK) day_reg |= 0x40; // бит 6 = 1
    buf[3] = day_reg;

    return DS3232_WriteReg(0x07, buf, 4);
}

HAL_StatusTypeDef DS3232_EnableAlarm1(uint8_t enable) {
    uint8_t control;
    HAL_StatusTypeDef status = DS3232_ReadReg(0x0E, &control, 1);
    if (status != HAL_OK) return status;
    if (enable) control |= 0x01;   // бит A1IE
    else control &= ~0x01;
    return DS3232_WriteReg(0x0E, &control, 1);
}

HAL_StatusTypeDef DS3232_ClearAlarm1Flag(void) {
    uint8_t status;
    HAL_StatusTypeDef st = DS3232_ReadReg(0x0F, &status, 1);
    if (st != HAL_OK) return st;
    status &= ~0x01;   // сброс бита A1F
    return DS3232_WriteReg(0x0F, &status, 1);
}

// ======================= Alarm2 =======================
HAL_StatusTypeDef DS3232_GetAlarm2(DS3232_Alarm2 *alarm) {
    uint8_t buf[3]; // регистры 0x0B..0x0D
    HAL_StatusTypeDef status = DS3232_ReadReg(0x0B, buf, 3);
    if (status != HAL_OK) return status;

    unpack_alarm_value(buf[0], &alarm->minute, &alarm->mask_min);
    unpack_alarm_value(buf[1], &alarm->hour,   &alarm->mask_hour);

    uint8_t day_reg = buf[2];
    alarm->mask_day = (day_reg >> 7) & 1;
    alarm->day_type = (day_reg >> 6) & 1;
    alarm->day = (alarm->mask_day) ? (day_reg & 0x3F) : bcd_to_bin(day_reg & 0x3F);

    return HAL_OK;
}

HAL_StatusTypeDef DS3232_SetAlarm2(const DS3232_Alarm2 *alarm) {
    uint8_t buf[3];
    buf[0] = pack_alarm_value(alarm->minute, alarm->mask_min);
    buf[1] = pack_alarm_value(alarm->hour,   alarm->mask_hour);

    uint8_t day_reg = pack_alarm_value(alarm->day, alarm->mask_day);
    if (alarm->day_type == ALARM_DAY_WEEK) day_reg |= 0x40;
    buf[2] = day_reg;

    return DS3232_WriteReg(0x0B, buf, 3);
}

HAL_StatusTypeDef DS3232_EnableAlarm2(uint8_t enable) {
    uint8_t control;
    HAL_StatusTypeDef status = DS3232_ReadReg(0x0E, &control, 1);
    if (status != HAL_OK) return status;
    if (enable) control |= 0x02;   // бит A2IE
    else control &= ~0x02;
    return DS3232_WriteReg(0x0E, &control, 1);
}

HAL_StatusTypeDef DS3232_ClearAlarm2Flag(void) {
    uint8_t status;
    HAL_StatusTypeDef st = DS3232_ReadReg(0x0F, &status, 1);
    if (st != HAL_OK) return st;
    status &= ~0x02;   // сброс бита A2F
    return DS3232_WriteReg(0x0F, &status, 1);
}
