#include "radio_rda5807m.h"
#include "main.h"
#include "eeprom.h"
#include <string.h>
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;
extern void IR_DebugPrint(void *decoder, const char *fmt, ...);
extern void *ir_decoder;
extern void radio_show_message(const char *msg, uint32_t duration_ms);

// --- Определения для сканера ---
#define MAX_STATIONS 60
typedef struct {
    uint16_t frequency;
    uint8_t rssi;
} FM_Station_t;

Radio_Memory_t radio_mem;

static FM_Station_t found_stations[MAX_STATIONS];
static uint8_t stations_count = 0;

static void sync_memory_to_eeprom(void);

// --- Внутреннее состояние ---
static bool initialized = false;
RDA5807_Config_t radio = {908, 1, 8, false, true};

// Переменные RDS
char rds_ps[9] = "        ";
static char pending_ps[9] = "        ";
static uint8_t hits_ps[4] = {0};
char rds_rt[65] = {0};
static char rt_pending[65] = {0};
uint8_t rds_hours = 0, rds_minutes = 0;
bool rds_time_updated = false;

// Вспомогательные прототипы
static void RDA5807M_UpdateAll(bool trigger_tune);
static void RDA5807M_WriteCtrl(uint16_t ctrl);
static uint16_t RDA5807M_ReadStatus(void);
void RDA5807M_ClearHardwareFIFO(void);

// --- Системные функции ---

static void RDA5807M_UpdateAll(bool trigger_tune) {
    uint8_t data[8];

    // 0x02: Контроль
    uint16_t reg02 = 0xC001 | (radio.bass ? 0x1000 : 0) | 0x0008; // База + RDS
    if (radio.mute) reg02 &= ~0x4000;

    // 0x03: Частота + ДИАПАЗОН + ШАГ
    uint16_t chan = (radio.freq_x10 >= 870) ? (radio.freq_x10 - 870) : 0;
    // Бит 3:2 = 00 (87-108), Бит 1:0 = 00 (100kHz)
    uint16_t reg03 = (chan << 6) | 0x0000;
    if (trigger_tune) reg03 |= 0x0010; // TUNE=1

    // 0x05: Громкость
    uint16_t reg05 = 0x8880 | (radio.seek_threshold << 8) | (radio.volume & 0x0F);

    data[0] = reg02 >> 8; data[1] = reg02 & 0xFF;
    data[2] = reg03 >> 8; data[3] = reg03 & 0xFF;
    data[4] = 0x00;       data[5] = 0x00;
    data[6] = reg05 >> 8; data[7] = reg05 & 0xFF;

    HAL_I2C_Master_Transmit(&hi2c1, 0x20, data, 8, 100);
}


static void RDA5807M_WriteCtrl(uint16_t ctrl) {
    uint8_t data[2] = {ctrl >> 8, ctrl & 0xFF};
    HAL_I2C_Master_Transmit(&hi2c1, 0x20, data, 2, 100);
}

static uint16_t RDA5807M_ReadStatus(void) {
    uint8_t buf[2];
    HAL_I2C_Master_Receive(&hi2c1, 0x23, buf, 2, 100);
    return (buf[0] << 8) | buf[1];
}

// --- Управление ---

void RDA5807M_Init(void) {
    if (initialized) return;

    // 1. Hard Reset (добавлены квадратные скобки [])
    uint8_t rst[] = {0x00, 0x02};
    HAL_I2C_Master_Transmit(&hi2c1, 0x20, rst, 2, 100);
    HAL_Delay(250);

    // 2. Первая запись (добавлены квадратные скобки [])
    uint16_t reg02 = 0xC001 | 0x0008;
    uint8_t init_pkt[] = { reg02 >> 8, reg02 & 0xFF, 0x00, 0x00 };
    HAL_I2C_Master_Transmit(&hi2c1, 0x20, init_pkt, 4, 100);

    HAL_Delay(50);
    initialized = true;
}



void RDA5807M_SetFrequency(uint16_t freq_x10) {
    RDA5807M_ResetRDS();
    radio.freq_x10 = freq_x10;

    // 1. Первая попытка настройки
    RDA5807M_UpdateAll(true);
    HAL_Delay(50);

    // 2. "Пинок" синтезатора (повторный TUNE часто лечит зависание PLL)
    RDA5807M_UpdateAll(true);

    HAL_Delay(150);
    RDA5807M_ClearHardwareFIFO();
}


void RDA5807M_SetVolume(uint8_t volume) {
    radio.volume = (volume > 15) ? 15 : volume;
    RDA5807M_UpdateAll(false);
}

void RDA5807M_Mute(bool mute) {
    if (mute) {
        for (int8_t v = radio.volume; v >= 0; v--) {
            RDA5807M_SetVolume(v);
            HAL_Delay(10);
        }
        RDA5807M_WriteCtrl(RDA5807M_BASE_CTRL & ~RDA5807M_DMUTE);
        radio.mute = true;
    } else {
        RDA5807M_WriteCtrl(RDA5807M_BASE_CTRL | RDA5807M_DMUTE);
        radio.mute = false;
        for (int8_t v = 0; v <= radio.volume; v++) {
            RDA5807M_SetVolume(v);
            HAL_Delay(10);
        }
    }
}

// --- Поиск и Скан ---

void RDA5807M_SeekNext(void) {
    // 1. Формируем команду поиска вверх
    uint16_t reg02 = 0xC001 | RDA5807M_BASS | RDA5807M_SEEK | RDA5807M_SEEKUP | (1 << 3);
    RDA5807M_WriteCtrl(reg02);

    // 2. Ждем завершения (бит STC в 0x0A)
    uint32_t start = HAL_GetTick();
    while (!(RDA5807M_ReadStatus() & 0x4000)) {
        if (HAL_GetTick() - start > 5000) break; // Увеличим таймаут для полного прохода
        HAL_Delay(50);
    }

    // 3. Сбрасываем бит SEEK, чтобы тюнер "успокоился"
    RDA5807M_WriteCtrl(reg02 & ~RDA5807M_SEEK);

    // 4. СИНХРОНИЗАЦИЯ: Просто берем значение Int. Умножать на 10 НЕ НУЖНО!
    radio.freq_x10 = RDA5807M_GetFrequencyInt();

    // 5. Очистка для новой станции
    RDA5807M_ResetRDS();
    HAL_Delay(150);
    RDA5807M_ClearHardwareFIFO();
}

void RDA5807M_SeekPrev(void) {
    // SEEKUP = 0 (поиск вниз)
    uint16_t reg02 = 0xC001 | RDA5807M_BASS | RDA5807M_SEEK | (1 << 3);
    RDA5807M_WriteCtrl(reg02);

    uint32_t start = HAL_GetTick();
    while (!(RDA5807M_ReadStatus() & 0x4000)) {
        if (HAL_GetTick() - start > 5000) break;
        HAL_Delay(50);
    }

    RDA5807M_WriteCtrl(reg02 & ~RDA5807M_SEEK);

    // СИНХРОНИЗАЦИЯ: Берем чистое целое число
    radio.freq_x10 = RDA5807M_GetFrequencyInt();

    RDA5807M_ResetRDS();
    HAL_Delay(150);
    RDA5807M_ClearHardwareFIFO();
}


void RDA5807M_Scan(void) {
    stations_count = 0;
    RDA5807M_Mute(true);
    for (uint16_t f = RDA5807M_FREQ_MIN; f <= RDA5807M_FREQ_MAX; f++) {
        RDA5807M_SetFrequency(f);
        HAL_Delay(70);
        if (RDA5807M_IsTuned() && RDA5807M_GetRSSI() > 25) {
            if (stations_count < MAX_STATIONS) {
                found_stations[stations_count].frequency = f;
                found_stations[stations_count].rssi = RDA5807M_GetRSSI();
                stations_count++;
            }
            f += 4;
        }
    }
    RDA5807M_Mute(false);
}

void RDA5807M_AutoScanAndStore(void) {
    FM_Station_t temp_list[MAX_STATIONS];
    uint8_t found_count = 0;

    IR_DebugPrint(&ir_decoder, "Smart Search Started...\n");
    RDA5807M_Mute(true);

    // 1. Сбор всех доступных станций
    for (uint16_t f = RDA5807M_FREQ_MIN; f <= RDA5807M_FREQ_MAX; f++) {
        RDA5807M_SetFrequency(f);
        HAL_Delay(70);

        uint8_t rssi = RDA5807M_GetRSSI();
        if (RDA5807M_IsTuned() && rssi > 30) {
            if (found_count < MAX_STATIONS) {
                temp_list[found_count].frequency = f;
                temp_list[found_count].rssi = rssi;
                found_count++;
                f += 4; // Пропуск зеркальных каналов
            }
        }
    }

    // 2. Сортировка по мощности (RSSI), чтобы в ТОП-20 попали лучшие
    for (int i = 0; i < found_count - 1; i++) {
        for (int j = 0; j < found_count - i - 1; j++) {
            if (temp_list[j].rssi < temp_list[j + 1].rssi) {
                FM_Station_t temp = temp_list[j];
                temp_list[j] = temp_list[j+1];
                temp_list[j+1] = temp;
            }
        }
    }

    // 3. Заполнение пресетов 1-20
    memset(radio_mem.freq, 0, sizeof(radio_mem.freq)); // Очищаем старое
    uint8_t to_store = (found_count > PRESETS_COUNT) ? PRESETS_COUNT : found_count;

    for (uint8_t i = 0; i < to_store; i++) {
        radio_mem.freq[i] = temp_list[i].frequency;
    }

    // 4. Сохраняем результат в EEPROM
    radio_mem.current_idx = 1; // Включаем первую найденную станцию
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    EEPROM_WriteBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

    // Включаем первую станцию
    if (to_store > 0) {
        RDA5807M_LoadPreset(1);
    }

    RDA5807M_Mute(false);
    IR_DebugPrint(&ir_decoder, "Smart Search Done. Stored %d stations.\n", to_store);
}



// Сохранить текущую частоту в ячейку (idx: 1..20)
void RDA5807M_StorePreset(uint8_t idx) {
    if (idx < 1 || idx > PRESETS_COUNT) return;

    // ВАЖНО: Мы должны вычесть 1, чтобы попасть в массив от 0 до 19!
    radio_mem.freq[idx - 1] = radio.freq_x10;

    radio_mem.current_idx = idx;

    // Сохранение в EEPROM
    EEPROM_WriteBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));

    IR_DebugPrint(&ir_decoder, "Preset %d stored: %d.%d MHz\n",
                  idx, radio.freq_x10 / 10, radio.freq_x10 % 10);
}


// Загрузить пресет (idx: 1..20)
void RDA5807M_LoadPreset(uint8_t idx) {
    if (idx < 1 || idx > PRESETS_COUNT) return;

    uint16_t f = radio_mem.freq[idx - 1];
    if (f >= RDA5807M_FREQ_MIN && f <= RDA5807M_FREQ_MAX) {
        radio_mem.current_idx = idx;
        RDA5807M_SetFrequency(f);

        // [!] Сохраняем новое состояние в EEPROM
        sync_memory_to_eeprom();

        IR_DebugPrint(&ir_decoder, "Load Preset %d: %d.%d MHz\n", idx, f/10, f%10);
    }
}

void Radio_NextPreset(void) {
    // Начинаем поиск со следующего индекса или с 1, если мы были в ручном режиме
    uint8_t start_idx = (radio_mem.current_idx == 0) ? 1 : radio_mem.current_idx;

    for (uint8_t i = 1; i <= PRESETS_COUNT; i++) {
        uint8_t next = ((start_idx + i - 1) % PRESETS_COUNT) + 1;
        if (!RDA5807M_IsPresetEmpty(next)) {
            RDA5807M_LoadPreset(next);
            return;
        }
    }
}

void Radio_PrevPreset(void) {
    uint8_t start_idx = (radio_mem.current_idx == 0) ? 1 : radio_mem.current_idx;

    for (uint8_t i = 1; i <= PRESETS_COUNT; i++) {
        // Движемся назад
        uint8_t prev = ((start_idx - i - 1 + PRESETS_COUNT) % PRESETS_COUNT) + 1;
        if (!RDA5807M_IsPresetEmpty(prev)) {
            RDA5807M_LoadPreset(prev);
            return;
        }
    }
}


void Radio_ConfirmStore(void) {
    if (radio_mem.current_idx != 0) return;

    uint8_t target_idx = 1; // По умолчанию пишем в 1, если все занято

    // Ищем первую пустую ячейку
    for (uint8_t i = 1; i <= PRESETS_COUNT; i++) {
        if (RDA5807M_IsPresetEmpty(i)) {
            target_idx = i;
            break;
        }
    }

    // 1. Сохраняем станцию
    RDA5807M_StorePreset(target_idx);

    // 2. ДАЕМ ПАУЗУ EEPROM (хоть там и есть задержка, для I2C очереди это полезно)
    HAL_Delay(50);

    // 3. Принудительно перезаписываем частоту в чип, чтобы зафиксировать ее
    RDA5807M_SetFrequency(radio.freq_x10);

    // 4. Показываем на дисплее красивое сообщение
    char store_msg[16];
    snprintf(store_msg, sizeof(store_msg), "Saved to %d", target_idx);
    radio_show_message(store_msg, 1500);
    RDA5807M_SetFrequency(radio.freq_x10);
}



// Проверка: есть ли что-то в ячейке (для зажигания цифр на VFD)
bool RDA5807M_IsPresetEmpty(uint8_t idx) {
    if (idx < 1 || idx > PRESETS_COUNT) return true;
    uint16_t f = radio_mem.freq[idx - 1];
    return (f < RDA5807M_FREQ_MIN || f > RDA5807M_FREQ_MAX);
}

// Внутри функции ручного изменения частоты (которую вызывает крест влево/вправо)
void Radio_ManualStep(bool up) {
    if (up) {
        if (radio.freq_x10 < RDA5807M_FREQ_MAX) radio.freq_x10++;
    } else {
        if (radio.freq_x10 > RDA5807M_FREQ_MIN) radio.freq_x10--;
    }

    radio_mem.current_idx = 0;
    radio_mem.last_manual_freq = radio.freq_x10;

    // ВРЕМЕННО ЗАКОММЕНТИРУЙ ЭТО! Будем писать реже.
    // sync_memory_to_eeprom();

    RDA5807M_UpdateAll(true); // Даем команду чипу
}



// --- Геттеры ---
uint16_t RDA5807M_GetFrequencyInt(void) {
    uint8_t buf[2];
    // Всегда читаем по адресу 0x11 (0x23)
    if (HAL_I2C_Master_Receive(&hi2c1, 0x23, buf, 2, 100) != HAL_OK) return 870;

    // В регистре 0x0A канал занимает младшие 10 бит (9:0)
    uint16_t reg0A = (buf[0] << 8) | buf[1];
    uint16_t channel = reg0A & 0x03FF;

    // Защита: если канал слишком большой для FM (больше 210 шагов),
    // значит мы прочитали не тот регистр. Возвращаем последнюю известную частоту.
    if (channel > 210) return radio.freq_x10;

    return 870 + channel;
}


uint8_t RDA5807M_GetRSSI(void) {
    uint8_t buf[4];
    HAL_I2C_Master_Receive(&hi2c1, 0x23, buf, 4, 100);
    return (buf[2] >> 1);
}

bool RDA5807M_IsTuned(void)  { uint8_t b[4]; HAL_I2C_Master_Receive(&hi2c1, 0x23, b, 4, 100); return (b[0] & 0x40) && (b[2] & 0x01); }
bool RDA5807M_IsStereo(void) { uint8_t b[2]; HAL_I2C_Master_Receive(&hi2c1, 0x23, b, 2, 100); return (b[0] & 0x04); }
bool RDA5807M_IsMute(void)   { return radio.mute; }
uint8_t RDA5807M_GetVolume(void) { return radio.volume; }

// --- RDS логика ---

void RDA5807M_ResetRDS(void) {

    // Делаем фиктивное чтение 12 байт, чтобы "прокашлять" старые данные из I2C-буфера
    uint8_t dummy[12];
    HAL_I2C_Master_Receive(&hi2c1, 0x23, dummy, 12, 100);

    memset(rds_ps, ' ', 8); rds_ps[8] = '\0';
    memset(rds_rt, 0, 65); memset(rt_pending, 0, 65);
    memset(pending_ps, 0, sizeof(pending_ps));
    memset(hits_ps, 0, 4); rds_time_updated = false;
}

void RDA5807M_ProcessRDS(void) {
    uint8_t buf[14]; // Читаем 14 байт, чтобы захватить регистр 0x10
    static uint8_t old_flag_2a = 0;

    // Читаем пакет (0x0A - 0x10)
    if (HAL_I2C_Master_Receive(&hi2c1, 0x23, buf, 14, 100) != HAL_OK) return;

    // Склеиваем важные регистры
    uint16_t reg0A = (buf[0] << 8) | buf[1];
    uint16_t reg0B = (buf[2] << 8) | buf[3];
    uint16_t blk_a = (buf[4] << 8) | buf[5]; // 0x0C
    uint16_t blk_b = (buf[6] << 8) | buf[7]; // 0x0D
    uint16_t blk_c = (buf[8] << 8) | buf[9]; // 0x0E
    uint16_t blk_d = (buf[10] << 8) | buf[11]; // 0x0F
    uint16_t reg10 = (buf[12] << 8) | buf[13];

    // 1. Продвинутый фильтр ошибок:
    // RDSR(15) должен быть 1.
    // Ошибки в 0x0B (младшие 4 бита) и 0x10 (старшие 4 бита) должны быть 0 для идеального приема.
    if (!(reg0A & 0x8000)) return;
    if ((reg0B & 0x000F) != 0 || (reg10 & 0xF000) != 0) return;

    uint8_t group_type = blk_b >> 12;
    uint8_t version = (blk_b >> 11) & 1;

    switch (group_type) {
        case 0: { // Название станции (PS)
            uint8_t idx = blk_b & 0x03;
            char c1 = (char)(blk_d >> 8), c2 = (char)(blk_d & 0xFF);
            if (c1 < 32 || c1 > 126) c1 = ' '; if (c2 < 32 || c2 > 126) c2 = ' ';

            // Валидация hits (оставляем нашу наработку для стабильности)
            if (c1 == pending_ps[idx*2] && c2 == pending_ps[idx*2+1]) {
                if (++hits_ps[idx] >= 2) {
                    rds_ps[idx*2] = c1; rds_ps[idx*2+1] = c2;
                    rds_ps[8] = '\0';
                }
            } else {
                hits_ps[idx] = 0; pending_ps[idx*2] = c1; pending_ps[idx*2+1] = c2;
            }
            break;
        }

        case 2: { // Радиотекст (RT)
            if (version != 0) break;
            // Проверка флага обновления текста (бит 4 в блоке B)
            uint8_t flag_2a = (blk_b & (1 << 4)) ? 1 : 0;
            if (flag_2a != old_flag_2a) {
                old_flag_2a = flag_2a;
                memset(rds_rt, ' ', 64); // Очистка при смене песни
            }
            uint8_t seg = blk_b & 0x0F;
            char t[4] = {(char)(blk_c >> 8), (char)(blk_c & 0xFF), (char)(blk_d >> 8), (char)(blk_d & 0xFF)};
            for (int i=0; i<4; i++) {
                if (seg*4+i < 64) rds_rt[seg*4+i] = (t[i] >= 32 && t[i] <= 126) ? t[i] : ' ';
            }
            break;
        }

        case 4: { // Время и дата (CT)
            if (version != 0) break;
            uint32_t MJD = ((uint32_t)(blk_b & 0x03) << 15) | (uint32_t)(blk_c >> 1);
            uint32_t hours = ((blk_c & 1) << 4) | (blk_d >> 12);
            uint32_t minutes = (blk_d >> 6) & 0x3F;
            uint8_t offset = blk_d & 0x1F; // Смещение (кратное 30 мин)

            // Пересчет MJD в UnixTime (упрощенно для текущего дня)
            // Полный пересчет в дату сделаем через функцию unixtime_to_datetime
            uint32_t unix_time = (MJD - 40587) * 86400;
            unix_time += hours * 3600 + minutes * 60;

            // Коррекция смещения
            if (blk_d & (1 << 5)) unix_time -= offset * 1800; // Минус
            else unix_time += offset * 1800;                // Плюс

            // Вытаскиваем финальные часы/минуты
            rds_hours = (unix_time / 3600) % 24;
            rds_minutes = (unix_time / 60) % 60;
            rds_time_updated = true;
            break;
        }
    }
}

void RDA5807M_GetDateTime(uint16_t *y, uint8_t *m, uint8_t *d) {
    // Вставь сюда логику RDA5807_unixtime_to_datetime из новой библиотеки
    // Она позволит тебе получить 2024 год, 05 месяц и т.д.
}


void RDA5807M_GetRDSStatus(RDA5807M_RDS_Status_t *s) {
    uint8_t b[12];
    if (HAL_I2C_Master_Receive(&hi2c1, 0x23, b, 12, 100) != HAL_OK) return;
    s->is_sync = (b[0] & 0x10);
    if (s->is_sync) {
        uint16_t bb = (b[6] << 8) | b[7];
        s->pty = (bb >> 5) & 0x1F;
        s->tp = (bb & 0x0400);
        memcpy(s->ps_name, rds_ps, 9);
    }
}

void RDA5807M_ClearHardwareFIFO(void) {
    uint16_t bbits = (radio.freq_x10 < 760) ? RDA5807M_BAND_EAST : RDA5807M_BAND_WORLD;
    uint8_t data[6] = {0xC0, 0x09, bbits >> 8, bbits & 0xFF, 0x30, 0x00};
    HAL_I2C_Master_Transmit(&hi2c1, 0x20, data, 6, 100);
    HAL_Delay(20);
    data[4] = 0x10; HAL_I2C_Master_Transmit(&hi2c1, 0x20, data, 6, 100);
}

void RDA5807M_PrintDiagnostic(void) {
    uint8_t b[12];
    HAL_I2C_Master_Receive(&hi2c1, 0x23, b, 12, 100);
    IR_DebugPrint(&ir_decoder, "SIG:%d ST:%d\n RDS:%d PS:[%s]\n RT:[%s]\n",
                  b[2]>>1, !!(b[0]&0x04), !!(b[0]&0x10), rds_ps, rds_rt);
}

// Включает RDS и настраивает FIFO
// В функции UpdateAll убедись, что RDS_EN включен в reg02 ВСЕГДА,
// если он был активирован один раз.
// Но НЕ используй trigger_tune=true при работе с RDS.

void RDA5807M_EnableRDS(void) {
    RDA5807M_ResetRDS(); // Это очистит rds_ps, hits_ps и pending_ps

    // Принудительно забиваем пробелами, чтобы snprintf не подхватил мусор
    for(int i=0; i<8; i++) rds_ps[i] = ' ';
    rds_ps[8] = '\0';

    // Просто обновляем регистры с включенным битом RDS_EN (бит 3 в 0x02)
    // ВАЖНО: передаем false, чтобы не перезапускать синтезатор частоты!
    RDA5807M_UpdateAll(false);

    IR_DebugPrint(&ir_decoder, "RDS Decoder Enabled\r\n");
}


// Установка порога чувствительности поиска
void RDA5807M_SetSeekThreshold(uint8_t threshold) {
    radio.seek_threshold = (threshold > 31) ? 31 : threshold;
    RDA5807M_UpdateAll(false);
}

// Мягкое включение
void RDA5807M_PowerOn(uint8_t target_volume) {
    if (initialized) return;
    RDA5807M_Init();

    // Плавный подъем громкости
    for (uint8_t v = 0; v <= target_volume; v++) {
        RDA5807M_SetVolume(v);
        HAL_Delay(30);
    }
}

// Бесшумное выключение
void RDA5807M_PowerOff(void) {
    // 1. Плавно снижаем громкость до 0
    for (int8_t v = radio.volume; v >= 0; v--) {
        RDA5807M_SetVolume(v);
        HAL_Delay(20);
    }
    // 2. Выключаем чип (ENABLE=0)
    uint8_t off_data[2] = {0x00, 0x00};
    HAL_I2C_Master_Transmit(&hi2c1, 0x20, off_data, 2, 100);
    initialized = false;
}

void RDA5807M_DumpRegisters(void) {
    uint16_t freq_val = RDA5807M_GetFrequencyInt();
    // Печатаем через целые числа, чтобы исключить глюки float
    IR_DebugPrint(&ir_decoder, "Current Freq: %d.%d MHz\n", freq_val / 10, freq_val % 10);
}

bool RDA5807M_HasPSData(void) {
    for (int i = 0; i < 8; i++) {
        // Если нашли любой символ, который не пробел и не ноль
        if (rds_ps[i] != ' ' && rds_ps[i] != '\0') {
            return true;
        }
    }
    return false;
}


// Очистка выбранного пресета (то, что вызывается на кнопке Clear)
void RDA5807M_ClearPreset(uint8_t idx) {
    if (idx < 1 || idx > PRESETS_COUNT) return;
    radio_mem.freq[idx - 1] = 0; // Сбрасываем частоту

    // Если мы удалили текущий активный пресет, переходим в ручной режим
    if (radio_mem.current_idx == idx) {
        radio_mem.current_idx = 0;
    }

    // Перезаписываем обновленное состояние в EEPROM
    EEPROM_WriteBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));
}

// Переключение режима EX BASS
void RDA5807M_ToggleBass(void) {
    // Инвертируем флаг в нашей структуре
    radio.bass = !radio.bass;

    // Отправляем новое состояние в чип (UpdateAll подхватит измененный radio.bass)
    RDA5807M_UpdateAll(false);

    IR_DebugPrint(&ir_decoder, "EX BASS: %s\n", radio.bass ? "ON" : "OFF");
}

// Геттер для дисплея
bool RDA5807M_IsBassOn(void) {
    return radio.bass;
}

// Возвращает количество заполненных ячеек (из 20 возможных)
uint8_t RDA5807M_GetCount(void) {
    uint8_t count = 0;
    for (uint8_t i = 1; i <= PRESETS_COUNT; i++) {
        if (!RDA5807M_IsPresetEmpty(i)) {
            count++;
        }
    }
    return count;
}

void RDA5807M_LoadStationsFromEEPROM(void) {
    // Читаем всю структуру целиком по адресу 0x1000
    if (EEPROM_ReadBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t)) == HAL_OK) {

        // Защита от "пустой" EEPROM (0xFFFF)
        if (radio_mem.current_idx == 0xFF) {
            memset(&radio_mem, 0, sizeof(Radio_Memory_t));
            return;
        }

        IR_DebugPrint(&ir_decoder, "EEPROM: Presets loaded successfully.\n");
    } else {
        IR_DebugPrint(&ir_decoder, "EEPROM: Failed to load presets.\n");
    }
}

static void sync_memory_to_eeprom(void) {
    EEPROM_WriteBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));
}

