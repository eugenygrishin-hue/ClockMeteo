#ifndef Radio_H
#define Radio_H

#include "stm32f4xx_hal.h"
#include <stdarg.h>
#include <stdbool.h>

#define TEF_I2C_ADDR    (0x64 << 1)

#define PRESETS_COUNT 20
// Параметры диапазона
#define Radio_FREQ_MIN    870
#define Radio_FREQ_MAX    1080

//
#define PREVIEW_TIME_MS 6000 // Время прослушивания одной станции (3 секунды)
//
#define RADIO_STORAGE_START   0x1000
#define RADIO_COUNT_ADDR      RADIO_STORAGE_START
#define RADIO_LIST_ADDR       (RADIO_STORAGE_START + 2)

typedef struct {
    uint16_t freq[PRESETS_COUNT]; // Частоты ячеек 1-20 (храним как 887 для 88.7)
    uint8_t  current_idx;         // Номер активного пресета (1..20, или 0 если ручной режим)
    uint16_t last_manual_freq;    // Последняя частота вне пресетов
    uint8_t   rds_enabled;        // [!] Флаг: включен ли RDS пользователем
} Radio_Memory_t;

typedef struct {
    uint16_t freq_x100;
    uint8_t  volume;
    uint8_t  seek_threshold;
    bool     mute;
    bool     bass;
} Config_t;

typedef struct {
    bool     is_sync;
    bool     is_ready;
    uint8_t  pty;
    bool     tp;
    bool     ta;
    bool     eon;
    char     ps_name[9];
} RDS_Status_t;

typedef struct {
    char text[65];               // 64 символа текста + 1 на терминатор '\0'
    uint16_t valid_segments;     // 16 бит. Каждый бит = один принятый сегмент строки
    uint8_t last_text_ab_flag;   // Хранит состояние флага Текст A/B для детекции смены трека
    bool is_ready;               // Флаг готовности строки для запуска бегущей строки
} RDS_RadioText_t;

// Делаем структуру доступной для FSM и планировщика
extern RDS_RadioText_t rds_rt;


// Объявляем структуру для внешних файлов
extern Radio_Memory_t radio_mem;
extern Config_t radio;

// Инициализация тюнера (загрузка DSP_INIT)
void Radio_Init(void);

// Установка частоты (freq в формате 10530 = 105.30 МГц)
void Radio_SetFrequency(uint16_t freq);

// Установка громкости (0...100)
void Radio_SetVolume(uint8_t vol);

// Mute / Unmute
void Radio_SetMute(bool mute);

// Получение уровня сигнала в dBuV
int16_t Radio_GetLevel(void);


void Radio_PowerOn(void);
void Radio_PowerOff(void);
void Radio_SetVolume(uint8_t vol);
void Radio_SetFrequency(uint16_t freq); // На вход ждем x100 (10530)
void Radio_SeekNext(void);
void Radio_SeekPrev(void);
uint16_t Radio_GetFrequency(void);      // Должна возвращать x100

bool Radio_IsTuned(void);
bool Radio_IsStereo(void);
bool Radio_IsMute(void);
bool Radio_GetRDSStatus(void);
bool Radio_IsPresetEmpty(uint8_t idx);

void Radio_LoadPreset(uint8_t idx);

// Заглушки для совместимости со старым кодом
bool Radio_HasPSData(void);                   // Пока RDS не настроен
void Radio_SetSeekThreshold(uint8_t th);      // У TEF это настраивается иначе
void Radio_PrintDiagnostic(void);
void Radio_AutoScanAndStore(void);
void Radio_ClearPreset(uint8_t idx);
uint8_t Radio_GetVolume(void);                // current_vol должен быть в драйвере
void Radio_Mute(bool mute);
void Radio_EnableRDS(void);

extern bool is_bass_on; // Только обещание, что переменная существует
extern bool is_seeking;

extern uint8_t rds_pty;
extern bool rds_tp;
extern int16_t rds_data[8];

void Radio_ToggleBass(void);
bool Radio_IsBassOn(void);

void Radio_ProcessRDS(void);
void Radio_Seek_Service(void);

void Radio_ResetStereoState(void);

void RDS_ResetRadioText(void);
void RDS_UpdateScroll(void); // На всякий случай и её тоже, если её там нет

extern uint8_t rt_scroll_pos;
extern uint32_t last_scroll_time;
extern uint16_t scroll_delay;

#endif
