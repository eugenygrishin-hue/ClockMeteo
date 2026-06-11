#include "sensor_manager.h"
#include "millis.h"
#include <string.h>

static Sensor_t* sensor_pool[MAX_SENSORS];
static uint8_t sensor_count = 0;

bool SensorManager_RegisterSensor(Sensor_t* sensor) {
    if (sensor_count >= MAX_SENSORS || sensor == NULL) return false;
    sensor_pool[sensor_count++] = sensor;
    return true;
}

void SensorManager_Init(void) {
    for (uint8_t i = 0; i < sensor_count; i++) {
        Sensor_t* s = sensor_pool[i];
        if (s->init(s->instance)) {
            s->status = SENSOR_OK;
        } else {
            s->status = SENSOR_ERROR;
        }
        s->last_poll_time = Millis_Get();
    }
}

void SensorManager_Process(void) {
    uint32_t now = Millis_Get();
    for (uint8_t i = 0; i < sensor_count; i++) {
        Sensor_t* s = sensor_pool[i];
        if (!s->enabled) continue;                     // пропускаем отключённые
        if ((now - s->last_poll_time) >= s->poll_interval_ms) {
            if (s->read(s->instance)) {
                s->status = SENSOR_OK;
            } else {
                s->status = SENSOR_ERROR;
            }
            s->last_poll_time = now;
        }
    }
}

Sensor_t* SensorManager_GetSensor(const char* name) {
    for (uint8_t i = 0; i < sensor_count; i++) {
        if (strcmp(sensor_pool[i]->name, name) == 0) return sensor_pool[i];
    }
    return NULL;
}

uint8_t SensorManager_GetCount(void) {
    return sensor_count;
}

Sensor_t* SensorManager_GetByIndex(uint8_t index) {
    if (index < sensor_count) return sensor_pool[index];
    return NULL;
}

bool SensorManager_EnableSensor(const char* name, bool enable) {
    Sensor_t* s = SensorManager_GetSensor(name);
    if (s == NULL) return false;
    s->enabled = enable;
    return true;
}

bool SensorManager_SetPollInterval(const char* name, uint32_t new_interval_ms) {
    Sensor_t* s = SensorManager_GetSensor(name);
    if (s == NULL) return false;
    s->poll_interval_ms = new_interval_ms;
    return true;
}

bool SensorManager_IsSensorEnabled(const char* name) {
    Sensor_t* s = SensorManager_GetSensor(name);
    if (s == NULL) return false;
    return s->enabled;
}
