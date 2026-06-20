#include "tef6686.h"
#include "tef_init.h" // Твой массив DSP_INIT здесь
#include "main.h"
#include "eeprom.h"
#include "millis.h"
#include <string.h>
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1; // Укажи свой номер шины

extern void IR_DebugPrint(void *decoder, const char *fmt, ...);
extern void *ir_decoder;
extern void radio_show_message(const char *msg, uint32_t duration_ms);

extern volatile uint8_t radio_freq_updated;
static bool seek_up_dir = true;

bool is_bass_on = false; // Реальное выделение памяти
bool is_seeking = false;

char rds_ps_name[9] = "        "; // 8 пробелов + нуль-терминатор
bool rds_new_data = false;

uint8_t rds_pty = 0;
bool rds_tp = false;
int16_t rds_data[8];

// Переменная для хранения текущей частоты (чтобы не дергать I2C каждый раз)
//static uint16_t current_freq_x100 = 8870;

// --- Внутреннее состояние ---
Config_t radio = {8890, 1, 8, false, true};

// --- Определения для сканера ---
#define MAX_STATIONS 60
typedef struct {
    uint16_t frequency;
    uint8_t rssi;
} FM_Station_t;

Radio_Memory_t radio_mem;

char rds_ps[9] = "        "; // 8 пробелов + нуль-терминатор

RDS_RadioText_t rds_rt;

// Обновляем функцию глобального сброса радиотекста
void RDS_ResetRadioText(void) {
    memset(rds_rt.text, ' ', 64);
    rds_rt.text[0] = '\0';
    rds_rt.valid_segments = 0;
    rds_rt.last_text_ab_flag = 0xFF;
    rds_rt.is_ready = false;

    // СБРАСЫВАЕМ АНИМАЦИЮ ВНУТРИ МОДУЛЯ
    rt_scroll_pos = 0;
    scroll_delay = 350;
    last_scroll_time = 0;
}



void Radio_UpdateAll(void) {
    // Вызывается после ручного шага.
    // Можно обновить иконки или считать качество сигнала.
	radio_freq_updated = 1;
    //Radio_GetLevel();
}


// Низкоуровневая отправка команд (аналог Set_Cmd)
void TEF_SetCmd(uint8_t mdl, uint8_t cmd, uint8_t len, ...) {
    uint8_t buf[20];
    va_list args;
    va_start(args, len);

    buf[0] = mdl;
    buf[1] = cmd;
    buf[2] = 1; // Index

    for (uint8_t i = 0; i < len; i++) {
        uint16_t val = va_arg(args, int);
        buf[3 + i * 2] = (uint8_t)(val >> 8);
        buf[4 + i * 2] = (uint8_t)(val & 0xFF);
    }
    va_end(args);

    HAL_I2C_Master_Transmit(&hi2c1, TEF_I2C_ADDR, buf, (len * 2 + 3), 100);
}

// Получение данных (аналог Get_Cmd)
HAL_StatusTypeDef TEF_GetCmd(uint8_t mdl, uint8_t cmd, int16_t *receive, uint8_t len) {
    uint8_t reg[3] = {mdl, cmd, 1};
    uint8_t data[16];

    if (HAL_I2C_Master_Transmit(&hi2c1, TEF_I2C_ADDR, reg, 3, 100) == HAL_OK) {
        if (HAL_I2C_Master_Receive(&hi2c1, TEF_I2C_ADDR, data, len * 2, 100) == HAL_OK) {
            for (uint8_t i = 0; i < len; i++) {
                receive[i] = (int16_t)((data[i * 2] << 8) | data[i * 2 + 1]);
            }
            return HAL_OK; // Возвращаем успех
        }
    }
    return HAL_ERROR; // Возвращаем ошибку
}

uint16_t Radio_GetFrequencyInt(void) {
    // Для функции Seek: считываем реальную частоту из чипа
    int16_t quality[7];
    TEF_GetCmd(32, 128, quality, 7); // Читаем статус модуля 32
    // В некоторых прошивках частота возвращается в параметрах качества,
    // но если нет - возвращаем то, что устанавливали последним
    return radio.freq_x100;
}

void Radio_SetFrequency(uint16_t freq) {
    // В проекте частота x100 (105.30 -> 10530)
    // TEF ожидает частоту в единицах 10 кГц (10530 / 1 = 10530)
    // Если шаг 50 кГц, частота передается как есть.
	if (freq < 1000) freq *= 10;
    TEF_SetCmd(32, 1, 2, 1, freq);

    memset(rds_ps_name, ' ', 8);
    rds_ps_name[8] = '\0';

    radio_freq_updated = 1;
}

void Radio_SetMute(bool mute) {
    TEF_SetCmd(48, 11, 1, mute ? 1 : 0);
}

int16_t Radio_GetLevel(void) {
    int16_t quality[7];
    TEF_GetCmd(32, 128, quality, 7);
    return quality[1]; // Уровень сигнала в dBuV * 10
}

void TEF6686_Init (void) {
    const uint8_t *pa = DSP_INIT;
    while (1) {
        uint8_t len = *pa++;
        if (len == 0) break;

        if (len == 2 && *pa == 0xFF) {
            HAL_Delay(*(pa + 1));
            pa += 2;
        } else {
            // Добавляем проверку HAL_OK
            if (HAL_I2C_Master_Transmit(&hi2c1, TEF_I2C_ADDR, (uint8_t*)pa, len, 500) != HAL_OK) {
                // Если ошибка — пробуем еще раз или выводим в дебаг
                IR_DebugPrint(&ir_decoder, "I2C Init Error at len %d\n", len);
            }
            pa += len;
            HAL_Delay(1); // Маленькая пауза между командами патча
        }
    }
}

void TEF6686_ForceStereoSettings(void) {
	// 1. Устанавливаем режим Стерео: Mode 0 (Auto), Минимальная база 0, Максимальная 1000 (100%)
	// MDL 32, CMD 35: Set_Stereo_Options
	TEF_SetCmd(32, 35, 1, 0); // Режим Auto

	// 2. Раскрываем стереобазу на максимум (очень важно!)
	// MDL 32, CMD 36: Set_Stereo_Min
	TEF_SetCmd(32, 36, 1, 1000); // 1000 = 100% стереобаза

	// 3. Отключаем "Stereo Masking" (подавление шума за счет перехода в моно)
	// MDL 32, CMD 20: Set_MphSuppression
	TEF_SetCmd(32, 20, 1, 0); // 0 = Выкл

}

void I2C_Bus_Recovery(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Включаем тактирование порта (например, GPIOB, если I2C1 на PB6/PB7 или PB8/PB9)
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // 2. Настраиваем SCL как Output Open-Drain, SDA как Input
    GPIO_InitStruct.Pin = GPIO_PIN_6; // Замени на свой пин SCL
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_7; // Замени на свой пин SDA
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 3. Если SDA удерживается в LOW ведомым устройством...
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_RESET) {
        // Генерируем 9 тактов на линии SCL
        for (int i = 0; i < 9; i++) {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
            HAL_Delay(1); // Короткая задержка
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
            HAL_Delay(1);
        }

        // Генерируем условие STOP вручную: SDA Low -> High при SCL High
        // Для этого временно переводим SDA в Output Open-Drain
        GPIO_InitStruct.Pin = GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET); // STOP создан
    }
}


void Radio_Init(void) {
    // На всякий случай дублируем включение питания (прямая логика — SET)
    HAL_GPIO_WritePin(GPIOA, TUNER_RST_Pin, GPIO_PIN_SET);

    // Даем электролиту время зарядиться, а чипу — проснуться
    HAL_Delay(150);

    // Проверяем шину I2C
    if (HAL_I2C_IsDeviceReady(&hi2c1, TEF_I2C_ADDR, 5, 200) != HAL_OK) {
        IR_DebugPrint(&ir_decoder, "TEF6686 I2C ERROR (NO POWER?)\n");
        return;
    }

    // Загрузка патча
    TEF6686_Init();
    IR_DebugPrint(&ir_decoder, "Patch loaded.\n");
}


void Radio_PowerOn(void) {
    // 1. Физически ВКЛЮЧАЕМ транзистор питания
    HAL_GPIO_WritePin(GPIOA, TUNER_RST_Pin, GPIO_PIN_SET);

    // 2. Инициализация и патч
    Radio_Init();

    // 3. Установка параметров
    TEF_SetCmd(32, 10, 1, 1);  // Auto bandwidth

    // Защита масштаба перед отправкой в чип
    uint16_t f = radio.freq_x100;
    if (f > 10800) f /= 10;
    Radio_SetFrequency(f);

    // 4. ТИХИЙ СТАРТ: выставляем физический 0 в чип, но НЕ трогаем переменную radio.volume!
    int16_t vol_db = (0 * 35) - 500;
    TEF_SetCmd(48, 10, 1, vol_db); // Установка физического нуля (-50 дБ)
    Radio_SetMute(false);          // Открываем тракт

    // 5. Плавный разгон звука от 0 до сохраненного radio.volume
    // Шагаем с паузой 15мс между делениями (весь подъем займет около 0.2 секунды — мягко и без щелчка)
    for (uint8_t v = 0; v <= radio.volume; v++) {
        int16_t current_db = (v * 35) - 500;
        TEF_SetCmd(48, 10, 1, current_db);
        HAL_Delay(15);
    }
}


void Radio_PowerOff(void) {
    // 1. Плавное затухание от текущего уровня до 0
    for (int8_t v = radio.volume; v >= 0; v--) {
        int16_t current_db = (v * 35) - 500;
        TEF_SetCmd(48, 10, 1, current_db);
        HAL_Delay(10); // 10мс на шаг
    }

    // 2. Включаем физический Mute в чипе
    Radio_SetMute(true);
    HAL_Delay(10);

    // 3. Полностью ОБЕСТОЧИВАЕМ тюнер MOSFET-ключом (Прямая логика раскачки S8050 — RESET)
    HAL_GPIO_WritePin(GPIOA, TUNER_RST_Pin, GPIO_PIN_RESET);
}

uint16_t Radio_GetFrequency(void) {
    return radio.freq_x100;
}

void Radio_SetVolume(uint8_t vol) {
    radio.volume = vol; // Гарантируем, что переменная обновлена

    if (vol == 0) {
        TEF_SetCmd(48, 11, 1, 1); //физический Mute
    } else {
        // Наша новая формула для TEF (0-15 -> децибелы)
        int16_t vol_db = (vol * 35) - 500;

        TEF_SetCmd(48, 11, 1, 0);      // Unmute
        TEF_SetCmd(48, 10, 1, vol_db); // Установка дБ
    }
}


// Направление поиска: 1 - вверх, 0 - вниз
void Radio_Seek(uint8_t up) {
    is_seeking = true;
    seek_up_dir = up;
    // Перед началом поиска немного приглушаем звук, если хочешь "тихий" поиск
    // Но для начала оставим как есть, чтобы слышать процесс
}

void Radio_SeekNext(void) { seek_up_dir = true; Radio_Seek(1); }
void Radio_SeekPrev(void) { seek_up_dir = false; Radio_Seek(0); }

// Проверка настройки на станцию (Tuned)
bool Radio_IsTuned(void) {
    int16_t status;
    // MDL 32, CMD 128 (Get_Quality), параметр 4 (Offset) или 1 (Level)
    // Но лучше использовать CMD 133 (Interface Status)
    TEF_GetCmd(32, 133, &status, 1);
    // Бит 15: 1 = частота захвачена (STATION/TUNED)
    return (status & (1 << 15)) ? true : false;
}

// Проверка Стерео
// 1. Статические переменные для фильтрации (в начало файла или перед функцией)
static uint8_t stereo_confirm_counter = 0;
static bool current_stereo_state = false;

// Функция сброса флагов стерео (ОБЯЗАТЕЛЬНО вызывай её при смене частоты или старте Seek)
void Radio_ResetStereoState(void) {
    current_stereo_state = false;
    stereo_confirm_counter = 0;
}

// 2. Обновленная интеллектуальная функция проверки стерео
bool Radio_IsStereo(void) {
    int16_t st_info = 0;
    int16_t level = Radio_GetLevel();

    // Запрашиваем данные о пилот-тоне у тюнера (CMD 129)
    TEF_GetCmd(32, 129, &st_info, 1);

    // --- Логика гистерезиса и накопления ---

    if (!current_stereo_state) {
        // Условия для ВКЛЮЧЕНИЯ (строгие пороги)
        if (level > 320 && st_info > 50) {
            stereo_confirm_counter++;
            if (stereo_confirm_counter >= 5) { // 5 стабильных циклов опроса подряд
                current_stereo_state = true;
                stereo_confirm_counter = 0;
            }
        } else {
            if (stereo_confirm_counter > 0) stereo_confirm_counter--;
        }
    }
    else {
        // Условия для ВЫКЛЮЧЕНИЯ (заниженные пороги — гистерезис)
        if (level < 260 || st_info < 30) {
            stereo_confirm_counter++;
            if (stereo_confirm_counter >= 8) { // 8 циклов плохого сигнала подряд
                current_stereo_state = false;
                stereo_confirm_counter = 0;
            }
        } else {
            if (stereo_confirm_counter > 0) stereo_confirm_counter--;
        }
    }

    return current_stereo_state;
}



// Проверка Mute
bool Radio_IsMute(void) {
    // В TEF6686 статус Mute обычно хранится в MDL 48 (Audio)
    // Но проще хранить его во внутренней переменной драйвера
    return radio.mute;
}

// Статус RDS (есть ли новые данные в буфере)
bool Radio_GetRDSStatus(void) {
    int16_t rds_status;
    // MDL 32, CMD 131 (Get_RDS_Data)
    TEF_GetCmd(32, 131, &rds_status, 1);
    // Бит 15: 1 = RDS data available (есть что читать)
    return (rds_status & (1 << 15)) ? true : false;
}

// Проверка: есть ли что-то в ячейке (для зажигания цифр на VFD)
bool Radio_IsPresetEmpty(uint8_t idx) {
    if (idx < 1 || idx > PRESETS_COUNT) return true;
    uint16_t f = radio_mem.freq[idx - 1];
    return (f < Radio_FREQ_MIN || f > Radio_FREQ_MAX);
}

static void sync_memory_to_eeprom(void) {
    EEPROM_WriteBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));
}

// Загрузить пресет (idx: 1..20)
void Radio_LoadPreset(uint8_t idx) {
    if (idx < 1 || idx > PRESETS_COUNT) return;

    uint16_t f = radio_mem.freq[idx - 1];
    if (f >= Radio_FREQ_MIN && f <= Radio_FREQ_MAX) {
        radio_mem.current_idx = idx;
        Radio_SetFrequency(f);

        // [!] Сохраняем новое состояние в EEPROM
        sync_memory_to_eeprom();

        IR_DebugPrint(&ir_decoder, "Load Preset %d: %d.%d MHz\n", idx, f/10, f%10);
        Radio_UpdateAll();
    }
}

bool Radio_HasPSData(void) {
    // Проверяем первые 4 символа (обычно этого достаточно, чтобы понять, что PS прилетел)
    for (int i = 0; i < 8; i++) {
        if (rds_ps_name[i] != ' ' && rds_ps_name[i] != '\0') {
            return true; // Нашли реальный символ!
        }
    }
    return false;
}

void Radio_ClearPreset(uint8_t idx) {
    if (idx < 1 || idx > PRESETS_COUNT) return;
    radio_mem.freq[idx - 1] = 0; // Сбрасываем частоту

    // Если мы удалили текущий активный пресет, переходим в ручной режим
    if (radio_mem.current_idx == idx) {
        radio_mem.current_idx = 0;
    }

    // Перезаписываем обновленное состояние в EEPROM
    EEPROM_WriteBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));
}

uint8_t Radio_GetVolume(void) { return radio.volume; } // current_vol должен быть в драйвере
void Radio_Mute(bool mute) { Radio_SetMute(mute); }
void Radio_EnableRDS(void) { }

// Заглушки для совместимости со старым кодом
void Radio_SetSeekThreshold(uint8_t th) { }   // У TEF это настраивается иначе
void Radio_PrintDiagnostic(void) { }
void Radio_AutoScanAndStore(void) { }



void Radio_ToggleBass(void) {
    is_bass_on = !is_bass_on;
    TEF_SetCmd(48, 1, 1, is_bass_on ? 1 : 0);
}

bool Radio_IsBassOn(void) { return is_bass_on; }

void Radio_ProcessRDS(void) {
    if (TEF_GetCmd(32, 131, rds_data, 8) != HAL_OK) return;

    if (!(rds_data[0] & 0x8000)) return; // Нет новых данных

    // Соответствие блоков согласно твоему массиву rds_data:
    uint16_t block_b = (uint16_t)rds_data[2];
    uint16_t block_c = (uint16_t)rds_data[3]; // Добавили Блок C (нужен для RadioText)
    uint16_t block_d = (uint16_t)rds_data[4];

    // --- Извлекаем универсальные флаги из блока B ---
    rds_pty = (block_b >> 5) & 0x1F;
    rds_tp = (block_b & (1 << 10)) ? true : false;

    uint8_t group_type = (block_b >> 12) & 0x0F;

    // =================================================================
    // ГРУППА 0: Имя станции (PS Name) - Уже работает
    // =================================================================
    if (group_type == 0) {
        uint8_t address = block_b & 0x03;
        uint8_t char1 = (block_d >> 8) & 0xFF;
        uint8_t char2 = block_d & 0xFF;

        if (char1 >= 32 && char1 <= 126) rds_ps_name[address * 2] = char1;
        if (char2 >= 32 && char2 <= 126) rds_ps_name[address * 2 + 1] = char2;

        rds_new_data = true;
    }
    // =================================================================
    // ГРУППА 2: Радиотекст (RadioText) - НАШ НОВЫЙ ПАРСЕР
    // =================================================================
    else if (group_type == 2) {
        uint8_t version = (block_b >> 11) & 0x01;     // 0 = 2A, 1 = 2B
        uint8_t text_ab_flag = (block_b >> 4) & 0x01;  // Флаг смены трека
        uint8_t segment_addr = block_b & 0x0F;         // Индекс сегмента (0..15)

        // Детектируем смену песни радиостанцией (инверсия бита A/B)
        if (rds_rt.last_text_ab_flag != 0xFF && rds_rt.last_text_ab_flag != text_ab_flag) {
            memset(rds_rt.text, ' ', 64);
            rds_rt.text[0] = '\0';
            rds_rt.valid_segments = 0;
            rds_rt.is_ready = false;
        }
        rds_rt.last_text_ab_flag = text_ab_flag;

        // --- Версия 2A (Приходят 4 символа в Блоках C и D) ---
        if (version == 0) {
            uint16_t char_idx = segment_addr * 4;

            char c1 = (char)((block_c >> 8) & 0xFF);
            char c2 = (char)(block_c & 0xFF);
            char c3 = (char)((block_d >> 8) & 0xFF);
            char c4 = (char)(block_d & 0xFF);

            if (char_idx + 3 < 64) {
                // Если поймали 0x0D (CR) — принудительно завершаем строку
                if (c1 == 0x0D) { rds_rt.text[char_idx] = '\0'; rds_rt.is_ready = true; }
                else { rds_rt.text[char_idx] = (c1 >= 32 && c1 <= 126) ? c1 : ' '; }

                if (c2 == 0x0D && rds_rt.text[char_idx] != '\0') { rds_rt.text[char_idx+1] = '\0'; rds_rt.is_ready = true; }
                else if (rds_rt.text[char_idx] != '\0') { rds_rt.text[char_idx+1] = (c2 >= 32 && c2 <= 126) ? c2 : ' '; }

                if (c3 == 0x0D && rds_rt.text[char_idx+1] != '\0') { rds_rt.text[char_idx+2] = '\0'; rds_rt.is_ready = true; }
                else if (rds_rt.text[char_idx+1] != '\0') { rds_rt.text[char_idx+2] = (c3 >= 32 && c3 <= 126) ? c3 : ' '; }

                if (c4 == 0x0D && rds_rt.text[char_idx+2] != '\0') { rds_rt.text[char_idx+3] = '\0'; rds_rt.is_ready = true; }
                else if (rds_rt.text[char_idx+2] != '\0') { rds_rt.text[char_idx+3] = (c4 >= 32 && c4 <= 126) ? c4 : ' '; }
            }
            rds_rt.valid_segments |= (1 << segment_addr);
        }
        // --- Версия 2B (Приходят 2 символа только в Блоке D) ---
        else {
            uint16_t char_idx = segment_addr * 2;

            char c1 = (char)((block_d >> 8) & 0xFF);
            char c2 = (char)(block_d & 0xFF);

            if (char_idx + 1 < 64) {
                if (c1 == 0x0D) { rds_rt.text[char_idx] = '\0'; rds_rt.is_ready = true; }
                else { rds_rt.text[char_idx] = (c1 >= 32 && c1 <= 126) ? c1 : ' '; }

                if (c2 == 0x0D && rds_rt.text[char_idx] != '\0') { rds_rt.text[char_idx+1] = '\0'; rds_rt.is_ready = true; }
                else if (rds_rt.text[char_idx] != '\0') { rds_rt.text[char_idx+1] = (c2 >= 32 && c2 <= 126) ? c2 : ' '; }
            }
            rds_rt.valid_segments |= (1 << segment_addr);
        }

        // Авто-валидация по заполнению буфера (если не было символа 0x0D)
        if (!rds_rt.is_ready) {
            uint8_t cnt = 0;
            for (int i = 0; i < 16; i++) {
                if (rds_rt.valid_segments & (1 << i)) cnt++;
            }
            if (cnt >= 4) { // Приняли хотя бы 4 сегмента текста
                rds_rt.is_ready = true;
                if (rds_rt.text[63] != '\0') rds_rt.text[63] = '\0'; // Защитный ноль в конец
            }
        }
    }
}




// Заглушки для навигации
void Radio_NextPreset(void) {
    // Начинаем поиск со следующего индекса или с 1, если мы были в ручном режиме
    uint8_t start_idx = (radio_mem.current_idx == 0) ? 1 : radio_mem.current_idx;

    for (uint8_t i = 1; i <= PRESETS_COUNT; i++) {
        uint8_t next = ((start_idx + i - 1) % PRESETS_COUNT) + 1;
        if (!Radio_IsPresetEmpty(next)) {
            Radio_LoadPreset(next);
            return;
        }
    }
}

void Radio_PrevPreset(void) {
    uint8_t start_idx = (radio_mem.current_idx == 0) ? 1 : radio_mem.current_idx;

    for (uint8_t i = 1; i <= PRESETS_COUNT; i++) {
        // Движемся назад
        uint8_t prev = ((start_idx - i - 1 + PRESETS_COUNT) % PRESETS_COUNT) + 1;
        if (!Radio_IsPresetEmpty(prev)) {
            Radio_LoadPreset(prev);
            return;
        }
    }
}

void Radio_ManualStep(bool up) {
    if (up) radio.freq_x100 += 5;
    else radio.freq_x100 -= 5;

    // Границы FM
    if (radio.freq_x100 > 10800) radio.freq_x100 = 8750;
    if (radio.freq_x100 < 8750) radio.freq_x100 = 10800;

    Radio_SetFrequency(radio.freq_x100);
    Radio_UpdateAll();
}


void Radio_StorePreset(uint8_t idx) {
    if (idx < 1 || idx > 20) return;
    radio_mem.freq[idx - 1] = radio.freq_x100;
    radio_mem.current_idx = idx;
    EEPROM_WriteBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));
}

void Radio_ConfirmStore(void)  {
    if (radio_mem.current_idx != 0) return;

    uint8_t target_idx = 1; // По умолчанию пишем в 1, если все занято

    // Ищем первую пустую ячейку
    for (uint8_t i = 1; i <= PRESETS_COUNT; i++) {
        if (Radio_IsPresetEmpty(i)) {
            target_idx = i;
            break;
        }
    }

    // 1. Сохраняем станцию
    Radio_StorePreset(target_idx);

    // 2. ДАЕМ ПАУЗУ EEPROM (хоть там и есть задержка, для I2C очереди это полезно)
    HAL_Delay(50);

    // 3. Принудительно перезаписываем частоту в чип, чтобы зафиксировать ее
    Radio_SetFrequency(radio.freq_x100);

    // 4. Показываем на дисплее красивое сообщение
    char store_msg[16];
    snprintf(store_msg, sizeof(store_msg), "Saved to %d", target_idx);
    radio_show_message(store_msg, 1500);
    Radio_SetFrequency(radio.freq_x100);
}

void Radio_LoadStationsFromEEPROM(void) {
    // Просто читаем нашу структуру из EEPROM (как было раньше)
    EEPROM_ReadBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));
}


uint8_t Radio_GetCount(void) {
    // Возвращаем количество сохраненных станций
    uint8_t count = 0;
    for(int i=0; i<20; i++) {
        if(radio_mem.freq[i] >= 6500) count++;
    }
    return count;
}

void Radio_ResetRDS(void) {
    memset(rds_ps_name, ' ', 8);
    // Здесь же можно очистить буферы RadioText для TEF
}

void Radio_Seek_Service(void) {
    if (!is_seeking) return;

    static uint32_t last_step_tick = 0;
    if (Millis_Get() - last_step_tick < 80) return;
    last_step_tick = Millis_Get();

    if (seek_up_dir) radio.freq_x100 += 10;
    else radio.freq_x100 -= 10;

    if (radio.freq_x100 > 10800) radio.freq_x100 = 8750;
    if (radio.freq_x100 < 8750) radio.freq_x100 = 10800;

    TEF_SetCmd(32, 1, 2, 3, radio.freq_x100);
    HAL_Delay(50);

    int16_t q[7];
    TEF_GetCmd(32, 128, q, 7);
    int16_t level = q[1];

    IR_DebugPrint(&ir_decoder, "Seek: freq=%d, level=%d\n", radio.freq_x100, level);
    radio_freq_updated = 1;

    static uint8_t good_cnt = 0;
    if (level > 500) {   // Повышенный порог
        good_cnt++;
        if (good_cnt >= 2) {
            is_seeking = false;

            // Пост-коррекция только на ±100 кГц (+-два соседних шага)
            int16_t best_freq = radio.freq_x100;
            int16_t best_level = level;
            int16_t deltas[] = {-10, 0, 10};   // -100 кГц и +100 кГц
            for (int i = 0; i < 3; i++) {
                int16_t test_freq = radio.freq_x100 + deltas[i];
                if (test_freq < 8750 || test_freq > 10800) continue;
                TEF_SetCmd(32, 1, 2, 3, test_freq);
                HAL_Delay(30);
                int16_t test_q[7];
                TEF_GetCmd(32, 128, test_q, 7);
                int16_t test_level = test_q[1];
                IR_DebugPrint(&ir_decoder, "  peak: %d -> L=%d\n", test_freq, test_level);
                if (test_level > best_level) {
                    best_level = test_level;
                    best_freq = test_freq;
                }
            }
            TEF_SetCmd(32, 1, 2, 1, best_freq);
            radio.freq_x100 = best_freq;
            IR_DebugPrint(&ir_decoder, "!!! FOUND: %d.%02d MHz | L:%d (peak)\n",
                          best_freq/100, best_freq%100, best_level);
            radio_show_message("STATION OK", 1000);
            good_cnt = 0;
            return;
        }
    } else {
        good_cnt = 0;
    }
}

