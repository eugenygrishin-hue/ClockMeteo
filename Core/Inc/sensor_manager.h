#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "sensor.h"

#define MAX_SENSORS 5

void SensorManager_Init(void);
bool SensorManager_RegisterSensor(Sensor_t* sensor);
void SensorManager_Process(void);
Sensor_t* SensorManager_GetSensor(const char* name);
uint8_t SensorManager_GetCount(void);
Sensor_t* SensorManager_GetByIndex(uint8_t index);

// Новые функции для управления датчиками
bool SensorManager_EnableSensor(const char* name, bool enable);
bool SensorManager_SetPollInterval(const char* name, uint32_t new_interval_ms);
bool SensorManager_IsSensorEnabled(const char* name);

#endif
