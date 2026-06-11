#include "veml7700.h"
#include <string.h>

static I2C_HandleTypeDef *veml_i2c = NULL;

// Вспомогательная функция для чтения двух байт по I2C
static HAL_StatusTypeDef read_bytes(uint8_t reg, uint8_t *buf, uint16_t len) {
    return HAL_I2C_Mem_Read(veml_i2c, VEML7700_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

static HAL_StatusTypeDef write_bytes(uint8_t reg, uint8_t *data, uint16_t len) {
    return HAL_I2C_Mem_Write(veml_i2c, VEML7700_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, data, len, 100);
}

HAL_StatusTypeDef VEML7700_Init(I2C_HandleTypeDef *hi2c) {
    veml_i2c = hi2c;

    // Читаем текущую конфигурацию
    uint16_t conf;
    if (VEML7700_ReadReg(VEML7700_REG_ALS_CONF, &conf) != HAL_OK)
        return HAL_ERROR;

    // Сбрасываем бит SD и устанавливаем разумные параметры (GAIN=1, IT=100ms)
    conf &= ~(VEML7700_CONF_SD | VEML7700_CONF_GAIN_MASK | VEML7700_CONF_IT_MASK);
    conf |= VEML7700_CONF_GAIN_1 | VEML7700_CONF_IT_100;

    if (VEML7700_WriteReg(VEML7700_REG_ALS_CONF, conf) != HAL_OK)
        return HAL_ERROR;

    // Небольшая задержка для применения настроек
    HAL_Delay(10);

    return HAL_OK;
}

HAL_StatusTypeDef VEML7700_ReadReg(uint8_t reg, uint16_t *data) {
    uint8_t buf[2];
    if (read_bytes(reg, buf, 2) != HAL_OK)
        return HAL_ERROR;
    *data = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return HAL_OK;
}

HAL_StatusTypeDef VEML7700_WriteReg(uint8_t reg, uint16_t data) {
    uint8_t buf[2] = { data & 0xFF, (data >> 8) & 0xFF };
    return write_bytes(reg, buf, 2);
}

// Новая версия VEML7700_ReadALS – без ожидания
HAL_StatusTypeDef VEML7700_ReadALS(uint16_t *als) {
    return VEML7700_ReadReg(VEML7700_REG_ALS, als);
}

HAL_StatusTypeDef VEML7700_ReadWhite(uint16_t *white) {
    return VEML7700_ReadReg(VEML7700_REG_WHITE, white);
}

// Функция чтения конфигурации тоже не требует ожидания
HAL_StatusTypeDef VEML7700_GetGainAndIT(uint8_t *gain, uint8_t *it) {
    uint16_t conf;
    HAL_StatusTypeDef res = VEML7700_ReadReg(VEML7700_REG_ALS_CONF, &conf);
    if (res == HAL_OK) {
        *gain = (conf >> 11) & 0x03;
        *it   = (conf >> 6)  & 0x03;
    }
    return res;
}

float VEML7700_CalculateLux(uint16_t als, uint8_t gain, uint8_t it) {
    float gain_factor;
    switch (gain) {
        case 0: gain_factor = 1.0f; break;
        case 1: gain_factor = 2.0f; break;
        case 2: gain_factor = 0.125f; break; // 1/8
        case 3: gain_factor = 0.25f; break;  // 1/4
        default: gain_factor = 1.0f;
    }
    float it_factor;
    switch (it) {
        case 0: it_factor = 25.0f / 100.0f; break;
        case 1: it_factor = 50.0f / 100.0f; break;
        case 2: it_factor = 100.0f / 100.0f; break;
        case 3: it_factor = 200.0f / 100.0f; break;
        default: it_factor = 1.0f;
    }
    // Разрешение для GAIN=1, IT=100ms = 0.0576 lux/step
    return als * 0.0576f * it_factor / gain_factor;
}

HAL_StatusTypeDef VEML7700_ReadLux(float *lux) {
    uint16_t als;
    uint8_t gain, it;
    if (VEML7700_ReadALS(&als) != HAL_OK)
        return HAL_ERROR;
    if (VEML7700_GetGainAndIT(&gain, &it) != HAL_OK)
        return HAL_ERROR;
    *lux = VEML7700_CalculateLux(als, gain, it);
    return HAL_OK;
}
