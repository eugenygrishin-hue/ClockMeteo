#include "sensor_veml7700.h"
#include "veml7700.h"
#include <string.h>

typedef struct {
    I2C_HandleTypeDef* hi2c;
    float lux;
} VEML7700_Context_t;

static bool veml7700_sensor_init(void* instance) {
    VEML7700_Context_t* ctx = (VEML7700_Context_t*)instance;
    return (VEML7700_Init(ctx->hi2c) == HAL_OK);
}

static bool veml7700_sensor_read(void* instance) {
    VEML7700_Context_t* ctx = (VEML7700_Context_t*)instance;
    float lux;
    if (VEML7700_ReadLux(&lux) != HAL_OK)
        return false;
    ctx->lux = lux;
    return true;
}

static float veml7700_sensor_get_value(void* instance, uint8_t index) {
    VEML7700_Context_t* ctx = (VEML7700_Context_t*)instance;
    if (index == 0) return ctx->lux;
    return 0.0f;
}

Sensor_t* VEML7700_CreateSensor(I2C_HandleTypeDef* hi2c, uint32_t poll_interval_ms) {
    static VEML7700_Context_t context;
    static Sensor_t sensor;

    memset(&context, 0, sizeof(context));
    context.hi2c = hi2c;

    sensor.instance = &context;
    sensor.init = veml7700_sensor_init;
    sensor.read = veml7700_sensor_read;
    sensor.get_value = veml7700_sensor_get_value;
    sensor.poll_interval_ms = poll_interval_ms;
    sensor.value_count = 1;
    sensor.name = "VEML7700";
    sensor.status = SENSOR_NOT_READY;
    sensor.last_poll_time = 0;

    return &sensor;
}
