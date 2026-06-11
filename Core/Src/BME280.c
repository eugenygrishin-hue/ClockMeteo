/**
  * @file  bme280.c
  * @brief Реализация библиотеки BME280 (исправленная версия)
  */
#include "bme280.h"

//------------------------------------------------
// Статические переменные модуля
static I2C_HandleTypeDef *bme_i2c = NULL;
static BME280_CalibData calib;
static int32_t t_fine;   // промежуточное значение температуры

//------------------------------------------------
// Обработка ошибок (можно переопределить под свои нужды)
static void Error_Handler(void) {
  while(1);  // заглушка
}

//------------------------------------------------
// Низкоуровневые функции I2C
static HAL_StatusTypeDef I2C_WriteReg(uint8_t reg, uint8_t value) {
  return HAL_I2C_Mem_Write(bme_i2c, BME280_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100);
}

static HAL_StatusTypeDef I2C_ReadReg(uint8_t reg, uint8_t *value) {
  return HAL_I2C_Mem_Read(bme_i2c, BME280_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, value, 1, 100);
}

static HAL_StatusTypeDef I2C_ReadRegs(uint8_t reg, uint8_t *buf, uint16_t len) {
  return HAL_I2C_Mem_Read(bme_i2c, BME280_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

//------------------------------------------------
// Чтение калибровочных коэффициентов (проверено в тесте)
static HAL_StatusTypeDef BME280_ReadCoefficients(void) {
  HAL_StatusTypeDef status;

  status = I2C_ReadRegs(BME280_REG_DIG_T1, (uint8_t*)&calib.dig_T1, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_T2, (uint8_t*)&calib.dig_T2, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_T3, (uint8_t*)&calib.dig_T3, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P1, (uint8_t*)&calib.dig_P1, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P2, (uint8_t*)&calib.dig_P2, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P3, (uint8_t*)&calib.dig_P3, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P4, (uint8_t*)&calib.dig_P4, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P5, (uint8_t*)&calib.dig_P5, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P6, (uint8_t*)&calib.dig_P6, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P7, (uint8_t*)&calib.dig_P7, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P8, (uint8_t*)&calib.dig_P8, 2); if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_P9, (uint8_t*)&calib.dig_P9, 2); if (status != HAL_OK) return status;
  status = I2C_ReadReg(BME280_REG_DIG_H1, &calib.dig_H1);               if (status != HAL_OK) return status;
  status = I2C_ReadRegs(BME280_REG_DIG_H2, (uint8_t*)&calib.dig_H2, 2); if (status != HAL_OK) return status;
  status = I2C_ReadReg(BME280_REG_DIG_H3, &calib.dig_H3);               if (status != HAL_OK) return status;

  // Чтение H4, H5 (3 байта)
  uint8_t h4_h5_h6[3];
  status = I2C_ReadRegs(BME280_REG_DIG_H4, h4_h5_h6, 3);
  if (status != HAL_OK) return status;
  calib.dig_H4 = (int16_t)(((uint16_t)h4_h5_h6[0] << 4) | (h4_h5_h6[1] & 0x0F));
  calib.dig_H5 = (int16_t)(((uint16_t)h4_h5_h6[2] << 4) | ((h4_h5_h6[1] >> 4) & 0x0F));

  status = I2C_ReadReg(BME280_REG_DIG_H6, (uint8_t*)&calib.dig_H6);
  if (status != HAL_OK) return status;

  return HAL_OK;
}

//------------------------------------------------
// Чтение всех сырых данных за одну транзакцию (как в тесте)
static HAL_StatusTypeDef BME280_ReadRaw(uint32_t *temp_raw, uint32_t *press_raw, uint16_t *hum_raw) {
  uint8_t buf[8];
  HAL_StatusTypeDef status = I2C_ReadRegs(BME280_REG_PRESS_MSB, buf, 8);
  if (status != HAL_OK) return status;

  // Данные в big-endian
  if (press_raw) *press_raw = ((uint32_t)buf[0] << 12) | ((uint32_t)buf[1] << 4) | (buf[2] >> 4);
  if (temp_raw)  *temp_raw  = ((uint32_t)buf[3] << 12) | ((uint32_t)buf[4] << 4) | (buf[5] >> 4);
  if (hum_raw)   *hum_raw   = ((uint16_t)buf[6] << 8) | buf[7];

  return HAL_OK;
}

//------------------------------------------------
// Компенсация температуры
static float BME280_CompensateTemp(int32_t temp_raw) {
  int32_t var1, var2;
  var1 = ((((temp_raw >> 3) - ((int32_t)calib.dig_T1 << 1))) * ((int32_t)calib.dig_T2)) >> 11;
  var2 = (((((temp_raw >> 4) - ((int32_t)calib.dig_T1)) * ((temp_raw >> 4) - ((int32_t)calib.dig_T1))) >> 12) * ((int32_t)calib.dig_T3)) >> 14;
  t_fine = var1 + var2;
  return (float)((t_fine * 5 + 128) >> 8) / 100.0f;
}

//------------------------------------------------
// Компенсация давления (возвращает давление в Па)
static float BME280_CompensatePress(int32_t press_raw) {
  int64_t var1, var2, p;
  var1 = ((int64_t)t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)calib.dig_P6;
  var2 = var2 + ((var1 * (int64_t)calib.dig_P5) << 17);
  var2 = var2 + ((int64_t)calib.dig_P4 << 35);
  var1 = ((var1 * var1 * (int64_t)calib.dig_P3) >> 8) + ((var1 * (int64_t)calib.dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calib.dig_P1) >> 33;
  if (var1 == 0) return 0;
  p = 1048576 - press_raw;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)calib.dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + ((int64_t)calib.dig_P7 << 4);
  return (float)p / 256.0f;   // давление в Па
}

//------------------------------------------------
// Компенсация влажности
static float BME280_CompensateHum(int16_t hum_raw) {
  int32_t v_x1_u32r;
  v_x1_u32r = (t_fine - ((int32_t)76800));
  v_x1_u32r = (((((hum_raw << 14) - (((int32_t)calib.dig_H4) << 20) - (((int32_t)calib.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
               (((((((v_x1_u32r * ((int32_t)calib.dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)calib.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * ((int32_t)calib.dig_H2) + 8192) >> 14));
  v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)calib.dig_H1)) >> 4));
  if (v_x1_u32r < 0) v_x1_u32r = 0;
  if (v_x1_u32r > 419430400) v_x1_u32r = 419430400;
  return (float)(v_x1_u32r >> 12) / 1024.0f;
}

//------------------------------------------------
// Публичные функции чтения
float BME280_ReadTemperature(void) {
  uint32_t temp_raw;
  if (BME280_ReadRaw(&temp_raw, NULL, NULL) != HAL_OK) {
    Error_Handler();
    return 0.0f;
  }
  return BME280_CompensateTemp((int32_t)temp_raw);
}

float BME280_ReadPressure(void) {
  uint32_t temp_raw, press_raw;
  if (BME280_ReadRaw(&temp_raw, &press_raw, NULL) != HAL_OK) {
    Error_Handler();
    return 0.0f;
  }
  BME280_CompensateTemp((int32_t)temp_raw);   // обновляем t_fine
  float press_pa = BME280_CompensatePress((int32_t)press_raw);
  return press_pa / 100.0f;   // возвращаем в гПа
}

float BME280_ReadHumidity(void) {
  uint32_t temp_raw;
  uint16_t hum_raw;
  if (BME280_ReadRaw(&temp_raw, NULL, &hum_raw) != HAL_OK) {
    Error_Handler();
    return 0.0f;
  }
  BME280_CompensateTemp((int32_t)temp_raw);   // обновляем t_fine
  return BME280_CompensateHum((int16_t)hum_raw);
}

float BME280_ReadAltitude(float seaLevel_hPa) {
  float pressure_hPa = BME280_ReadPressure();
  return 44330.0f * (1.0f - powf(pressure_hPa / seaLevel_hPa, 0.1903f));
}

//------------------------------------------------
// Функции настройки
void BME280_SetOversamplingHum(uint8_t osrs) {
  uint8_t reg;
  I2C_ReadReg(BME280_REG_CTRL_HUM, &reg);
  reg = (reg & ~0x07) | (osrs & 0x07);
  I2C_WriteReg(BME280_REG_CTRL_HUM, reg);
  // По даташиту, после изменения ctrl_hum нужно переписать ctrl_meas
  I2C_ReadReg(BME280_REG_CTRL_MEAS, &reg);
  I2C_WriteReg(BME280_REG_CTRL_MEAS, reg);
}

void BME280_SetOversamplingTemp(uint8_t osrs) {
  uint8_t reg;
  I2C_ReadReg(BME280_REG_CTRL_MEAS, &reg);
  reg = (reg & ~0xE0) | ((osrs & 0x07) << 5);
  I2C_WriteReg(BME280_REG_CTRL_MEAS, reg);
}

void BME280_SetOversamplingPress(uint8_t osrs) {
  uint8_t reg;
  I2C_ReadReg(BME280_REG_CTRL_MEAS, &reg);
  reg = (reg & ~0x1C) | ((osrs & 0x07) << 2);
  I2C_WriteReg(BME280_REG_CTRL_MEAS, reg);
}

void BME280_SetMode(uint8_t mode) {
  uint8_t reg;
  I2C_ReadReg(BME280_REG_CTRL_MEAS, &reg);
  reg = (reg & ~0x03) | (mode & 0x03);
  I2C_WriteReg(BME280_REG_CTRL_MEAS, reg);
}

void BME280_SetStandby(uint8_t tsb) {
  uint8_t reg;
  I2C_ReadReg(BME280_REG_CONFIG, &reg);
  reg = (reg & ~0xE0) | ((tsb & 0x07) << 5);
  I2C_WriteReg(BME280_REG_CONFIG, reg);
}

void BME280_SetFilter(uint8_t filter) {
  uint8_t reg;
  I2C_ReadReg(BME280_REG_CONFIG, &reg);
  reg = (reg & ~0x1C) | ((filter & 0x07) << 2);
  I2C_WriteReg(BME280_REG_CONFIG, reg);
}

//------------------------------------------------
// Инициализация
void BME280_Init(I2C_HandleTypeDef *hi2c) {
  bme_i2c = hi2c;
  HAL_StatusTypeDef status;
  uint8_t id;

  // Проверка ID
  status = I2C_ReadReg(BME280_REG_ID, &id);
  if (status != HAL_OK || id != BME280_ID) {
    Error_Handler();
  }

  // Мягкий сброс
  I2C_WriteReg(BME280_REG_RESET, 0xB6);
  HAL_Delay(10);

  // Ожидание окончания копирования калибровки
  uint8_t status_reg;
  do {
    I2C_ReadReg(BME280_REG_STATUS, &status_reg);
  } while (status_reg & 0x01);

  // Чтение калибровочных коэффициентов
  if (BME280_ReadCoefficients() != HAL_OK) {
    Error_Handler();
  }

  // Установка параметров по умолчанию (можно изменить)
  BME280_SetStandby(BME280_STBY_1000);
  BME280_SetFilter(BME280_FILTER_4);
  BME280_SetOversamplingTemp(BME280_OSRS_T_x4);
  BME280_SetOversamplingPress(BME280_OSRS_P_x2);
  BME280_SetOversamplingHum(BME280_OSRS_H_x1);
  BME280_SetMode(BME280_MODE_NORMAL);

  // Даём время для первого измерения
  HAL_Delay(50);
}
