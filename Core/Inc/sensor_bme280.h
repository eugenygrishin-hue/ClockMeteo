#ifndef SENSOR_BME280_H
#define SENSOR_BME280_H

#include "sensor.h"
#include "stm32f4xx_hal.h"   // для I2C_HandleTypeDef

Sensor_t* BME280_CreateSensor(I2C_HandleTypeDef* hi2c, uint32_t poll_interval_ms);

#endif
