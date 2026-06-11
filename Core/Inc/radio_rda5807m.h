#ifndef RADIO_RDA5807M_H
#define RADIO_RDA5807M_H

#include <stdint.h>
#include <stdbool.h>

// Адреса I2C (7-бит)
#define RDA5807M_ADDR_WRITE  0x10   // Sequential access
#define RDA5807M_ADDR_READ   0x11   // Random access

// Регистры
#define RDA5807M_REG_CTRL    0x02
#define RDA5807M_REG_FREQ    0x03
#define RDA5807M_REG_VOLUME  0x05
#define RDA5807M_REG_STATUS  0x0A
#define RDA5807M_REG_RSSI    0x0B

// Биты регистра CTRL (0x02)
#define RDA5807M_ENABLE      (1 << 0)
#define RDA5807M_DHIZ        (1 << 15)
#define RDA5807M_DMUTE       (1 << 14)
#define RDA5807M_BASS        (1 << 12)
#define RDA5807M_SEEK        (1 << 8)
#define RDA5807M_SEEKUP      (1 << 9)
#define RDA5807M_TUNE        (1 << 3)
#define RDA5807M_RDS_EN      (1 << 3)

// Параметры диапазона
#define RDA5807M_FREQ_MIN    870
#define RDA5807M_FREQ_MAX    1080

// BAND биты (регистр 0x03)
#define RDA5807M_BAND_US_EU  (0 << 2)   // 87–108 MHz
#define RDA5807M_BAND_WORLD  (2 << 2)   // 76–108 MHz
#define RDA5807M_BAND_EAST   (3 << 2)   // 65–76 MHz

#define RDA5807M_SPACE_100KHZ (0 << 0)
#define RDA5807M_BASE_CTRL    0xC001
#define RDA5807M_RDS_FIFO_CLR (1 << 13)

//
#define RADIO_STORAGE_START   0x1000
#define RADIO_COUNT_ADDR      RADIO_STORAGE_START
#define RADIO_LIST_ADDR       (RADIO_STORAGE_START + 2)

//
#define PREVIEW_TIME_MS 6000 // Время прослушивания одной станции (3 секунды)

// Прототипы функций для пульта (чтобы app_states их видел)
void RDA5807M_AutoScanAndStore(void);
void Radio_NextPreset(void);
void Radio_PrevPreset(void);
void Radio_ManualStep(bool up);
void Radio_ConfirmStore(void);
uint8_t  RDA5807M_GetCount(void);


typedef struct {
    uint16_t freq_x10;
    uint8_t  volume;
    uint8_t  seek_threshold;
    bool     mute;
    bool     bass;
} RDA5807_Config_t;

typedef struct {
    bool     is_sync;
    bool     is_ready;
    uint8_t  pty;
    bool     tp;
    bool     ta;
    bool     eon;
    char     ps_name[9];
} RDA5807M_RDS_Status_t;

#define PRESETS_COUNT 20

typedef struct {
    uint16_t freq[PRESETS_COUNT]; // Частоты ячеек 1-20 (храним как 887 для 88.7)
    uint8_t  current_idx;         // Номер активного пресета (1..20, или 0 если ручной режим)
    uint16_t last_manual_freq;    // Последняя частота вне пресетов
    bool     rds_enabled;         // [!] Флаг: включен ли RDS пользователем
} Radio_Memory_t;

// Функции для работы с пресетами
void     RDA5807M_StorePreset(uint8_t idx); // Сохранить текущую частоту в ячейку
void     RDA5807M_LoadPreset(uint8_t idx);  // Загрузить частоту из ячейки
void     RDA5807M_ClearPreset(uint8_t idx); // Очистить ячейку
bool     RDA5807M_IsPresetEmpty(uint8_t idx);
void     RDA5807M_LoadStationsFromEEPROM(void);

// Основное управление
void     RDA5807M_Init(void);
void     RDA5807M_PowerOn(uint8_t target_volume);
void     RDA5807M_PowerOff(void);

// Сеттеры
void     RDA5807M_SetFrequency(uint16_t freq_x10);
void     RDA5807M_SetVolume(uint8_t volume);
void     RDA5807M_Mute(bool mute);
void     RDA5807M_SetSeekThreshold(uint8_t threshold);
void     RDA5807M_SeekNext(void);
void     RDA5807M_SeekPrev(void);
void     RDA5807M_Scan(void);

// Геттеры
uint16_t RDA5807M_GetFrequencyInt(void);
uint8_t  RDA5807M_GetVolume(void);
uint8_t  RDA5807M_GetRSSI(void);
bool     RDA5807M_IsTuned(void);
bool     RDA5807M_IsStereo(void);
bool     RDA5807M_IsMute(void);

void     RDA5807M_ToggleBass(void);
bool     RDA5807M_IsBassOn(void);


// RDS
void     RDA5807M_ProcessRDS(void);
void     RDA5807M_GetRDSStatus(RDA5807M_RDS_Status_t *status);
void     RDA5807M_ResetRDS(void);
void     RDA5807M_EnableRDS(void);
bool     RDA5807M_HasPSData(void);

// Сервис
void     RDA5807M_PrintDiagnostic(void);
void     RDA5807M_DumpRegisters(void);

// Объявляем структуру для внешних файлов
extern Radio_Memory_t radio_mem;
extern RDA5807_Config_t radio;

#endif
