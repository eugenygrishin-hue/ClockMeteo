#include "alarm.h"
#include "eeprom.h"
#include "radio_rda5807m.h"
#include "app_states.h"
#include <string.h>

// Глобальное хранилище
Alarm_Storage_t alarm_db;

static bool alarm_is_ringing = false;
static uint32_t snooze_timer = 0;
static bool snooze_active = false;
static uint8_t last_checked_minute = 61; // Чтобы не срабатывать дважды в одну минуту

void Alarm_Init(void) {
    // Читаем настройки из EEPROM (адрес 0x1100, как договаривались)
    if (EEPROM_ReadBuffer(ALARM_STORAGE_ADDR, (uint8_t*)&alarm_db, sizeof(Alarm_Storage_t)) != HAL_OK) {
        memset(&alarm_db, 0, sizeof(Alarm_Storage_t));
    }

    // Валидация данных после чтения (на случай чистой EEPROM)
    for (int i = 0; i < ALARMS_COUNT; i++) {
        if (alarm_db.list[i].hour > 23) alarm_db.list[i].hour = 0;
        if (alarm_db.list[i].minute > 59) alarm_db.list[i].minute = 0;
    }
}

void Alarm_Save(void) {
    EEPROM_WriteBuffer(ALARM_STORAGE_ADDR, (uint8_t*)&alarm_db, sizeof(Alarm_Storage_t));
}

// Проверка будильников (вызывать каждую секунду в основном цикле)
void Alarm_Check(uint8_t h, uint8_t m, uint8_t s, uint8_t day_of_week) {
    // day_of_week от DS3231: 1=Пн ... 7=Вс
    if (day_of_week < 1 || day_of_week > 7) return;

    // 1. Логика Snooze
    if (snooze_active && HAL_GetTick() >= snooze_timer) {
        snooze_active = false;
        Alarm_StartRinging();
    }

    // 2. Логика срабатывания основного времени
    if (s == 0 && m != last_checked_minute) {
        last_checked_minute = m;
        uint8_t day_bit = (1 << (day_of_week - 1)); // Пн = бит 0

        for (int i = 0; i < ALARMS_COUNT; i++) {
            Alarm_t *a = &alarm_db.list[i];

            // Если будильник включен (бит 7) И активен в этот день (биты 0..6)
            if ((a->days & 0x80) && (a->days & day_bit)) {
                if (a->hour == h && a->minute == m) {
                    Alarm_StartRinging();
                }
            }
        }
    }
}

void Alarm_StartRinging(void) {
    alarm_is_ringing = true;
    snooze_active = false;

    // Включаем радио на последней частоте
    RDA5807M_PowerOn(3); // Громкость 3 для мягкого пробуждения
    radio_show_message("!!! ALARM !!!", 5000);
}

void Alarm_Stop(void) {
    alarm_is_ringing = false;
    snooze_active = false;
    RDA5807M_PowerOff();
    radio_show_message("Alarm Stop", 2000);
}

void Alarm_Snooze(void) {
    if (!alarm_is_ringing) return;

    alarm_is_ringing = false;
    snooze_active = true;
    snooze_timer = HAL_GetTick() + (2 * 60 * 1000); // 2 минуты

    RDA5807M_PowerOff();
    radio_show_message("Snooze 2 min", 2000);
}

bool Alarm_IsRinging(void) {
    return alarm_is_ringing;
}

bool Alarm_HasAnyActive(void) {
    for (int i = 0; i < 5; i++) {
        // Проверяем 7-й бит (флаг включения) в каждом будильнике
        if (alarm_db.list[i].days & 0x80) {
            return true;
        }
    }
    return false;
}

