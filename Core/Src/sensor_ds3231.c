#include "sensor_ds3231.h"
#include <string.h>

// Контекст датчика – хранит указатель на I2C и последнее прочитанное время
typedef struct {
    I2C_HandleTypeDef* hi2c;   // добавить указатель на I2C
    DS3232_Time time;
} DS3231_Context_t;

// Функция инициализации
static bool DS3231_Init(void* instance) {
    DS3231_Context_t* ctx = (DS3231_Context_t*)instance;

    // Инициализация библиотеки с сохранённым I2C
    DS3232_Init(ctx->hi2c);

    // Проверяем, отвечает ли датчик – читаем время
    if (DS3232_GetTime(&ctx->time) == HAL_OK) {
        return true;       // связь есть
    }
    return false;          // датчик не отвечает
}

// Функция чтения (опроса)
static bool DS3231_Read(void* instance) {
    DS3231_Context_t* ctx = (DS3231_Context_t*)instance;
    // Читаем текущее время из RTC
    return (DS3232_GetTime(&ctx->time) == HAL_OK);
}

// Функция получения значения по индексу
static float DS3231_GetValue(void* instance, uint8_t index) {
    DS3231_Context_t* ctx = (DS3231_Context_t*)instance;
    switch (index) {
        case 0: return (float)ctx->time.second;
        case 1: return (float)ctx->time.minute;
        case 2: return (float)ctx->time.hour;
        case 3: return (float)ctx->time.weekday;      // день недели (1-7)
        case 4: return (float)ctx->time.date;         // число месяца
        case 5: return (float)ctx->time.month;
        case 6: return (float)ctx->time.year;         // год (uint16_t)
        default: return 0.0f;
    }
}

// Создание и заполнение структуры Sensor_t
Sensor_t* DS3231_CreateSensor(I2C_HandleTypeDef* hi2c, uint32_t poll_interval_ms) {
    // Статическое выделение памяти для контекста и структуры датчика
    static DS3231_Context_t context;
    static Sensor_t sensor;

    // Обнуляем контекст и сохраняем указатель на I2C
    memset(&context, 0, sizeof(context));
    context.hi2c = hi2c;   // теперь hi2c сохранён

    sensor.instance = &context;
    sensor.init = DS3231_Init;
    sensor.read = DS3231_Read;
    sensor.get_value = DS3231_GetValue;
    sensor.poll_interval_ms = poll_interval_ms;
    sensor.value_count = 7;   // секунды, минуты, часы, день недели, дата, месяц, год
    sensor.name = "DS3231";
    sensor.status = SENSOR_NOT_READY;
    sensor.last_poll_time = 0;
    sensor.enabled = true;

    return &sensor;
}
