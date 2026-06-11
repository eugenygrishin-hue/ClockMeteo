#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SENSOR_OK,
    SENSOR_ERROR,
    SENSOR_NOT_READY
} SensorStatus_t;

typedef bool (*SensorInitFunc)(void* instance);
typedef bool (*SensorReadFunc)(void* instance);
typedef float (*SensorGetValueFunc)(void* instance, uint8_t value_index);

typedef struct {
    void* instance;
    SensorInitFunc init;
    SensorReadFunc read;
    SensorGetValueFunc get_value;
    SensorStatus_t status;
    uint32_t poll_interval_ms;
    uint32_t last_poll_time;
    uint8_t value_count;
    const char* name;
    bool enabled;          // новое поле: true – опрашивать, false – пропускать
} Sensor_t;

#endif
