#include "sensor_config.h"
#include "sensor_manager.h"
#include "sensor_bme280.h"
#include "sensor_ds3231.h"
#include "sensor_veml7700.h"

// Объявления внешних переменных (они определены в main.c)
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;

void SENSOR_InitAll(void)
{
    Sensor_t* bme280 = BME280_CreateSensor(&hi2c2, 2000);
    SensorManager_RegisterSensor(bme280);

    Sensor_t* ds3231 = DS3231_CreateSensor(&hi2c2, 1000);
    SensorManager_RegisterSensor(ds3231);

    Sensor_t* veml = VEML7700_CreateSensor(&hi2c3, 1000);
    SensorManager_RegisterSensor(veml);

    SensorManager_Init();
}
