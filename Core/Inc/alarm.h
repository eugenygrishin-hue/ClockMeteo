#ifndef ALARM_H
#define ALARM_H

#include <stdint.h>
#include <stdbool.h>

#define ALARMS_COUNT 5
#define ALARM_STORAGE_ADDR 0x1100

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t days;      // Bit 7: Active, Bits 0-6: Mon-Sun
    uint16_t freq;     // Частота в формате (Freq * 100). Например, 10530
} Alarm_t;

typedef struct {
    Alarm_t list[ALARMS_COUNT];
    uint8_t current_edit_idx; // Индекс редактируемого будильника (0..4)
    bool is_edit_mode;
} Alarm_Storage_t;

extern Alarm_Storage_t alarm_db;
extern uint8_t alarm_edit_pos;
extern uint8_t day_select_idx;


void Alarm_Init(void);
void Alarm_Save(void);
void Alarm_Check(uint8_t h, uint8_t m, uint8_t s, uint8_t day_of_week);
void Alarm_StartRinging(void);
void Alarm_Stop(void);
void Alarm_Snooze(void);
bool Alarm_IsRinging(void);
bool Alarm_HasAnyActive(void);

#endif
