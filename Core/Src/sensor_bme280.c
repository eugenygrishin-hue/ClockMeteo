#include "sensor_bme280.h"
#include "BME280.h"
#include <string.h>

// Контекст датчика – хранит указатель на I2C и последние значения
typedef struct {
    I2C_HandleTypeDef* hi2c;
    float temperature;
    float humidity;
    float pressure;
} BME280_Context_t;

// Инициализация датчика (теперь с уникальным именем)
static bool bme280_sensor_init(void* instance) {
    BME280_Context_t* ctx = (BME280_Context_t*)instance;

    // Вызов библиотечной функции инициализации с сохранённым I2C
    BME280_Init(ctx->hi2c);

    // Проверка связи через чтение температуры
    float t = BME280_ReadTemperature();
    if (t < -40.0f || t > 85.0f) {
        return false;   // датчик не отвечает или данные некорректны
    }

    // Сохраняем начальные значения
    ctx->temperature = t;
    ctx->humidity = BME280_ReadHumidity();
    ctx->pressure = BME280_ReadPressure();

    return true;
}

// Опрос датчика (чтение всех величин)
static bool BME280_Read(void* instance) {
    BME280_Context_t* ctx = (BME280_Context_t*)instance;

    ctx->temperature = BME280_ReadTemperature();
    ctx->humidity    = BME280_ReadHumidity();
    ctx->pressure    = BME280_ReadPressure();

    return true;
}

// Получение значения по индексу
static float BME280_GetValue(void* instance, uint8_t index) {
    BME280_Context_t* ctx = (BME280_Context_t*)instance;
    switch (index) {
        case 0: return ctx->temperature;
        case 1: return ctx->humidity;
        case 2: return ctx->pressure;
        default: return 0.0f;
    }
}

// Создание и заполнение структуры Sensor_t
Sensor_t* BME280_CreateSensor(I2C_HandleTypeDef* hi2c, uint32_t poll_interval_ms) {
    static BME280_Context_t context;
    static Sensor_t sensor;
    memset(&context, 0, sizeof(context));
    context.hi2c = hi2c;

    sensor.instance = &context;
    sensor.init = bme280_sensor_init;
    sensor.read = BME280_Read;
    sensor.get_value = BME280_GetValue;
    sensor.poll_interval_ms = poll_interval_ms;
    sensor.value_count = 3;
    sensor.name = "BME280";
    sensor.status = SENSOR_NOT_READY;
    sensor.last_poll_time = 0;
    sensor.enabled = true;               // <-- обязательно
    return &sensor;
}
