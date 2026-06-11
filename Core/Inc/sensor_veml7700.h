#ifndef SENSOR_VEML7700_H
#define SENSOR_VEML7700_H

#include "sensor.h"
#include "stm32f4xx_hal.h"

Sensor_t* VEML7700_CreateSensor(I2C_HandleTypeDef* hi2c, uint32_t poll_interval_ms);

#endif
