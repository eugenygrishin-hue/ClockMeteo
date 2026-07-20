#include "app_states.h"
#include "sensor_manager.h"
#include "app_config.h"
#include "app_ir.h"
#include "board.h"
#include "ds3232.h"
#include "millis.h"
#include "vfd_driver.h"
#include "app_display.h"
#include "archive.h"

#include "tef6686.h"
#include "task_scheduler.h"
#include "alarm.h"
#include "eeprom.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;

void Radio_ChangeVolume(bool up);
void Radio_ToggleMute(void);
void Radio_ToggleRDS(void);
void Radio_StartPreviewScan(void);
void Radio_StopPreviewScan(void);

void Radio_VolumeUp(void);
void Radio_VolumeDown(void);

// Позиции редактирования:
// 0: Номер будильника (1-5)
// 1: Десятки часов, 2: Единицы часов
// 3: Десятки минут, 4: Единицы минут
// 5: Режим дней недели (Пн-Вс)

uint8_t alarm_edit_pos = 0;   // 0:Номер, 1-2:Часы, 3-4:Минуты, 5:Дни
uint8_t day_select_idx = 0;   // 0..6 (Пн..Вс) для выбора конкретного дня

uint8_t radio_display_mode = 2; // По умолчанию пусть будет RadioText

// ------------------------------------------------------------------
// Глобальные переменные для режимов редактирования
// ------------------------------------------------------------------
static SystemState_t prev_state;
static uint32_t edit_entry_time;
static uint8_t edit_pos;
static uint16_t backup_alarm_freq; // Для отмены изменений
static uint32_t alarm_start_tick = 0;
static uint8_t current_vol = 1;
uint8_t current_ringing_alarm_idx = 0;

uint32_t snooze_time_ms = 0;
bool is_snooze_active = false;


// Таблица весов для каждого из 7 разрядов даты (edit_pos от 0 до 6)
// 0: дес. дней, 1: ед. дней, 2: месяц целиком, 3: тыс. лет, 4: сотни лет, 5: дес. лет, 6: ед. лет
const uint16_t date_weights[] = {10, 1, 1, 1000, 100, 10, 1};

static uint32_t last_volume_tick = 0;
static uint8_t current_alarm_vol = 0;
const uint32_t VOLUME_STEP_MS = 3000; // Шаг нарастания — 3 секунды
const uint8_t MAX_ALARM_VOL = 10;     // Порог комфортной громкости


static DS3232_Time orig_time;
static DS3232_Time edit_time;

uint8_t pict_brackets_r = 4;
uint8_t pict_equal = 3;
uint8_t pict_brackets_l = 2;

typedef struct {
    uint8_t *val;   // Указатель на поле (hour, minute или second)
    uint8_t limit;  // Максимальное значение для этого разряда + 1
    uint8_t weight; // Вес разряда (10 или 1)
} TimeDigit_t;

// Таблица правил для всех 6 позиций (0 - 5)
const TimeDigit_t time_digits[] = {
    { &edit_time.hour,   3, 10 }, // 0: Десятки часов (0, 1, 2)
    { &edit_time.hour,  10,  1 }, // 1: Единицы часов (0-9)
    { &edit_time.minute, 6, 10 }, // 2: Десятки минут (0-5)
    { &edit_time.minute,10,  1 }, // 3: Единицы минут (0-9)
    { &edit_time.second, 6, 10 }, // 4: Десятки секунд (0-5)
    { &edit_time.second,10,  1 }  // 5: Единицы секунд (0-9)
};


static struct {
    uint8_t day;
    uint8_t month;
    uint16_t year;
} edit_date;

// Вспомогательная переменная для ввода +10
static bool plus10_active = false;

// Переменные для радио
char radio_temp_msg[16] = {0};
uint32_t radio_msg_end = 0;
//static bool radio_mute_state = false;
bool rds_active = false;               // управляет задачей RDS
static uint8_t rds_enabled_by_user = 0; // флаг, что пользователь включил RDS (для сохранения)

static bool preview_active = false;
static uint32_t preview_timer = 0;

// Состояния индикаторов
bool last_tuned = false;
bool last_stereo = false;
bool last_mute = false;
bool last_rds_sync = false;

uint8_t radio_freq_updated = 0;

bool get_is_snooze_active(void) {
	return is_snooze_active;
}

void radio_show_message(const char *msg, uint32_t duration_ms) {
    strncpy(radio_temp_msg, msg, sizeof(radio_temp_msg) - 1);
    radio_temp_msg[sizeof(radio_temp_msg) - 1] = '\0';
    radio_msg_end = Millis_Get() + duration_ms;
}

// ------------------------------------------------------------------
// Вспомогательные функции
// ------------------------------------------------------------------

void enable_sensor(const char* name, bool enable, uint32_t interval_ms) {
    SensorManager_EnableSensor(name, enable);
    if (enable && interval_ms > 0) {
        SensorManager_SetPollInterval(name, interval_ms);
    }
}

static uint8_t get_days_in_month(uint8_t month, uint16_t year) {
    if (month == 2) {
        // Проверка на високосный год
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) return 29;
        return 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
    return 31;
}

static void edit_alarm_time_math(Alarm_t *a, bool inc) {
    // Временная структура для использования общего алгоритма
    if (alarm_edit_pos == 1) { // Десятки часов
        uint8_t tens = a->hour / 10;
        tens = (inc) ? (tens + 1) % 3 : (tens == 0 ? 2 : tens - 1);
        a->hour = (a->hour % 10) + (tens * 10);
        if (a->hour > 23) a->hour = 23;
    } else if (alarm_edit_pos == 2) { // Единицы часов
        uint8_t units = a->hour % 10;
        uint8_t limit = (a->hour / 10 == 2) ? 4 : 10;
        units = (inc) ? (units + 1) % limit : (units == 0 ? limit - 1 : units - 1);
        a->hour = (a->hour / 10) * 10 + units;
    } else if (alarm_edit_pos == 3) { // Десятки минут
        uint8_t tens = a->minute / 10;
        tens = (inc) ? (tens + 1) % 6 : (tens == 0 ? 5 : tens - 1);
        a->minute = (a->minute % 10) + (tens * 10);
    } else if (alarm_edit_pos == 4) { // Единицы минут
        uint8_t units = a->minute % 10;
        units = (inc) ? (units + 1) % 10 : (units == 0 ? 9 : units - 1);
        a->minute = (a->minute / 10) * 10 + units;
    }
}

void Radio_Common_HandleCommand(uint16_t addr, uint8_t cmd) {
	// Логика переключения пресетов (Крест: Вверх/Вниз)
	if (addr == 0x414E) {
		switch (cmd) {
		case 0x0B: Radio_SeekNext(); break;
		case 0x01: Radio_NextPreset(); break;
		case 0x81: Radio_PrevPreset(); break;
		case 0xC1: Radio_ManualStep(false); break;
		case 0x41: Radio_ManualStep(true); break;
		default: break;
		}
	}

	// Логика цифр (Основной пульт: 1-7/0)
	if (addr == 0x010E) {
		// Математика для кнопок 1-9 (убираем 9 веток switch!)
		const uint8_t num_keys[9] = { 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
				0x18, 0x98 };
		for (uint8_t i = 0; i < 9; i++) {
			if (cmd == num_keys[i]) {
				Radio_LoadPreset((i + 1) + (plus10_active ? 10 : 0));
				plus10_active = false;
				radio_freq_updated = 1;

				RDS_ResetRadioText();

				return; // Нашли кнопку — выходим из функции
			}
		}

		// Остальные команды пульта 0x010E
		switch (cmd) {
		case 0xDA: Radio_SeekPrev(); radio_show_message("", 0); radio_freq_updated = 1; RDS_ResetRadioText();
		break;
		case 0xE3: Radio_ChangeVolume(true); RDS_ResetRadioText();
		break;
		case 0x13: Radio_ChangeVolume(false);
		RDS_ResetRadioText();
		break;
		case 0xCB: Radio_PrintDiagnostic();
		RDS_ResetRadioText();
		break;
		case 0xB0: plus10_active = true; radio_freq_updated = 1;
		RDS_ResetRadioText();
		break;
		case 0x02:
			if (plus10_active) {
				Radio_LoadPreset(20);
				plus10_active = false;
				RDS_ResetRadioText();
			}
			radio_freq_updated = 1;
			break;
		default:
			break;
		}
	}
}

void Alarm_Volume_Service(void) {
    // Если мы только зашли в режим или громкость еще не максимальная
    if (current_alarm_vol < MAX_ALARM_VOL) {
        uint32_t now = Millis_Get();

        // Проверяем, пришло ли время для следующего шага
        if (now - last_volume_tick >= VOLUME_STEP_MS) {
            last_volume_tick = now;
            current_alarm_vol++;

            // Отправляем команду в TEF6686
            Radio_SetVolume(current_alarm_vol);

            // Опционально: вывод в дебаг для контроля
            // IR_DebugPrint(&ir_decoder, "Alarm Vol: %d\n", current_alarm_vol);
        }
    }
}


uint16_t Get_Radio_Current_Freq(void) {
    if (radio_mem.current_idx > 0 && radio_mem.current_idx <= 20) {
    	return radio_mem.freq[radio_mem.current_idx - 1]; // Используем .freq вместо .presets
    } else if (radio_mem.current_idx == 0 && radio_mem.last_manual_freq >= 870) {
        // Если выключили в режиме ручного поиска — включаем ту самую волну
        return radio_mem.last_manual_freq;
    } else {
        return 870; // Дефолтная частота
    }
}

void Radio_PowerOn_LowVolume(uint16_t frequency) {
    Radio_PowerOn();      // Питание чипа
    Radio_SetVolume(1);   // Минимальный уровень (чтобы слышать факт работы)
    Radio_SetFrequency(frequency);
    Radio_Mute(false);    // Снимаем заглушку
}

void Alarm_Check_Logic(uint8_t h, uint8_t m, uint8_t wd) {
    uint8_t day_bit;
    if ((wd == 7)||(wd == 0)) day_bit = 6; // Воскресенье (6-й бит)
    else day_bit = wd - 1;    // Пн(0), Вт(1)... Сб(5)

    for (int i = 0; i < 5; i++) {
        Alarm_t *al = &alarm_db.list[i];

        // Диагностика для поиска причины
        if (al->hour == h && al->minute == m) {
             IR_DebugPrint(&ir_decoder, "TIME MATCH! AlIdx:%d, Mask:0x%02X, TargetBit:0x%02X\n",
                           i+1, al->days, (1 << day_bit));
        }

        if ((al->days & 0x80) && (al->days & (1 << day_bit))) {
            if (al->hour == h && al->minute == m) {
                current_ringing_alarm_idx = i;
                StateMachine_SetState(STATE_ALARM_RINGING);
                IR_DebugPrint(&ir_decoder, "!!! STATE CHANGE TO RINGING !!!\n");
            }
        }
    }
}



// ------------------------------------------------------------------
// Обработка IR‑команд
// ------------------------------------------------------------------
static void on_main_process(void) {
    uint16_t addr;
    uint8_t cmd;

    if (APP_IR_GetCommand(&addr, &cmd)) {
        // --- Обработка пульта 0x414E (Крест и навигация) ---
        if (addr == 0x414E) {
            if (cmd == 0x21) { // ENTER: Настройка времени
                DS3232_GetTime(&orig_time);
                edit_time = orig_time;
                prev_state = STATE_MAIN;
                edit_pos = 0;
                edit_entry_time = Millis_Get();
                StateMachine_SetState(STATE_EDIT_TIME);
            } else if (cmd == 0x0A) { // Переход в РАДИО
                StateMachine_SetState(STATE_RADIO);
            }
        }

        // --- Обработка основного пульта 0x010E ---
        else if (addr == 0x010E) {
            if (cmd == 0xDB) { // Кнопка настройки БУДИЛЬНИКА
                alarm_edit_pos = 0; // Начинаем с выбора номера будильника
                day_select_idx = 0; // Сбрасываем выбор дня на Понедельник
                StateMachine_SetState(STATE_EDIT_ALARM);
            }
        }
    }
}

static void on_radio_process(void) {
	uint16_t addr;
	uint8_t cmd;

	// 1. Тайм-аут сообщений (Оригинал сохранен)
	/*if (radio_msg_end > 0 && Millis_Get() >= radio_msg_end && radio_temp_msg[0] != '\0') {
	 if (strcmp(radio_temp_msg, "Radio Off") == 0) {
	 StateMachine_SetState(STATE_MAIN);
	 radio_msg_end = 0;
	 radio_temp_msg[0] = '\0';
	 }
	 }*/

	// 2. Логика Preview Scan (Оригинал сохранен)
	if (preview_active && (Millis_Get() - preview_timer >= PREVIEW_TIME_MS)) {
		preview_timer = Millis_Get();
		Radio_NextPreset();
		Radio_ResetStereoState();
		radio_freq_updated = 1;
	}

	// 3. Обработка ИК-пульта
	if (APP_IR_GetCommand(&addr, &cmd)) {
		if (addr == 0x010E) {
			// Математика для кнопок 1-9 (убираем 9 веток switch!)
			const uint8_t num_keys[9] = { 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68,
					0xE8, 0x18, 0x98 };
			for (uint8_t i = 0; i < 9; i++) {
				if (cmd == num_keys[i]) {
					Radio_LoadPreset((i + 1) + (plus10_active ? 10 : 0));
					Radio_ResetStereoState();
					plus10_active = false;
					radio_freq_updated = 1;

					RDS_ResetRadioText();

					return; // Нашли кнопку — выходим из функции
				}
			}

			// Остальные команды пульта 0x010E
			switch (cmd) {
			//case 0x01: radio_show_message("Radio Off", 2000); radio_msg_end = Millis_Get() + 2000; break;
			case 0xDA:
				Radio_SeekPrev();
				Radio_ResetStereoState();
				radio_show_message("", 0);
				radio_freq_updated = 1;
				RDS_ResetRadioText();
				break;
			case 0xE3:
				Radio_ChangeVolume(true);
				RDS_ResetRadioText();
				break;
			case 0x13:
				Radio_ChangeVolume(false);
				break;
			case 0x83:
				Radio_ToggleMute();
				break;
			case 0x23:
				Radio_AutoScanAndStore();
				Radio_ResetStereoState();
				RDS_ResetRadioText();
				break;
			case 0x3B:
				Radio_ToggleRDS();
				RDS_ResetRadioText();
				// Показываем на VFD короткую подсказку, какой режим включился
				if (radio_display_mode == 0)
					radio_show_message("FREQ ONLY", 1000);
				else if (radio_display_mode == 1)
					radio_show_message("RDS STATION", 1000);
				else if (radio_display_mode == 2)
					radio_show_message("RDS TEXT", 1000);

				// Сразу же обновляем иконки, чтобы CD-диск мгновенно отреагировал на кнопку
				//update_radio_indicators();
				break;
			case 0xCB:
				Radio_PrintDiagnostic();
				break;
			case 0xB0:
				plus10_active = true;
				radio_freq_updated = 1;
				break;
			case 0x02:
				if (plus10_active) {
					Radio_LoadPreset(20);
					Radio_ResetStereoState();
					plus10_active = false;
				}
				radio_freq_updated = 1;
				break;
			case 0x39:
				Radio_ClearPreset(radio_mem.current_idx);
				radio_freq_updated = 1;
				RDS_ResetRadioText();
				break;
			default:
				break;
			}
		} else if (addr == 0x414E) {
			// Команды пульта 0x414E (включая крест и EX BASS)
			switch (cmd) {
			case 0x0B:
				Radio_SeekNext();
				Radio_ResetStereoState();
				RDS_ResetRadioText();
				break;
			case 0x01:
				Radio_NextPreset();
				Radio_ResetStereoState();
				RDS_ResetRadioText();
				break;
			case 0x81:
				Radio_PrevPreset();
				Radio_ResetStereoState();
				RDS_ResetRadioText();
				break;
			case 0xC1:
				Radio_ManualStep(false);
				Radio_ResetStereoState();
				RDS_ResetRadioText();
				break;
			case 0x41:
				Radio_ManualStep(true);
				Radio_ResetStereoState();
				RDS_ResetRadioText();
				break;
			case 0x21:
				Radio_ConfirmStore();
				radio_show_message("Stored", 2000);
				update_radio_indicators();
				break;
			case 0x69:
				Radio_ToggleBass();
				radio_show_message(
						Radio_IsBassOn() ? "EX BASS ON" : "EX BASS OFF", 1500);
				break;
			case 0xC9:
				Radio_StartPreviewScan();
				RDS_ResetRadioText();
				return; // Не останавливаем Preview сразу при его запуске
			default:
				break;
			}
			// Автоматически останавливаем Preview Scan при ЛЮБОМ действии на пульте 0x414E
			radio_show_message("", 0);
			radio_freq_updated = 1;
			Radio_StopPreviewScan();
		}

	}
}

void Radio_ChangeVolume(bool up) {
    // 1. Изменяем уровень громкости
    if (up) {
        if (radio.volume < 15) radio.volume++; // Наша шкала 0-15
    } else {
        if (radio.volume > 0) radio.volume--;
    }

    // 2. АВТО-ВЫХОД ИЗ MUTE:
    // Если радио было заглушено кнопкой Mute, сбрасываем этот логический флаг.
    // (Проверь имя переменной в твоей структуре 'radio', обычно это radio.mute)
    if (radio.mute) {
        radio.mute = false;
    }

    // 3. Физически применяем громкость к тюнеру (внутри само снимет физический Mute)
    Radio_SetVolume(radio.volume);

    // 4. Формируем сообщение для VFD
    char msg[16];
    snprintf(msg, sizeof(msg), "Vol %s %d", up ? "UP" : "DOWN", radio.volume);
    radio_show_message(msg, 2000);

    // Взводим флаг обновления экрана, чтобы новые значения и иконка Mute перерисовались мгновенно
    radio_freq_updated = 1;
}


void Radio_ToggleMute(void) {
    radio.mute = !radio.mute;
    Radio_Mute(radio.mute);
    radio_show_message(radio.mute ? "Mute" : "Unmute", 2000);
    radio_freq_updated = 1;
}
void Radio_ToggleRDS(void) {
    // Циклически переключаем 3 режима: 0 -> 1 -> 2 -> 0
    radio_display_mode = (radio_display_mode + 1) % 3;

    // Синхронизируем старые флаги активности для обратной совместимости с остальным кодом
    if (radio_display_mode == 0) {
        rds_active = false;
        rds_enabled_by_user = 0;
    } else {
        rds_active = true;
        rds_enabled_by_user = 1;
    }

    // Сохраняем текущий режим в структуру памяти для EEPROM
    radio_mem.rds_enabled = radio_display_mode;

    // Управляем железной частью тюнера и задачами планировщика
    if (rds_active) {
        Radio_EnableRDS();
        TaskScheduler_SetTaskEnabled("RDS", true);
        RDS_ResetRadioText();

        // Показываем пользователю, какой режим RDS сейчас включился
        if (radio_display_mode == 1) {
            radio_show_message("RDS STATION", 2000);
        } else if (radio_display_mode == 2) {
            radio_show_message("RDS TEXT LN", 2000);
        }
    } else {
        // Если перешли в режим 0 — полностью тушим RDS для экономии ресурсов
        TaskScheduler_SetTaskEnabled("RDS", false);
        radio_show_message("RDS Off", 2000);
        Radio_ResetRDS();
        RDS_ResetRadioText();
    }

    // Записываем настройки в EEPROM, чтобы режим не сбросился при выключении питания
    EEPROM_WriteBuffer(RADIO_STORAGE_START, (uint8_t*)&radio_mem, sizeof(Radio_Memory_t));
}


static void on_date_process(void) {
    uint16_t addr;
    uint8_t cmd;
    if (APP_IR_GetCommand(&addr, &cmd) && addr == 0x414E && cmd == 0x21) {
        DS3232_GetTime(&orig_time);
        edit_date.day = orig_time.date;
        edit_date.month = orig_time.month;
        edit_date.year = orig_time.year;
        prev_state = STATE_SHOW_DATE;
        edit_pos = 0;
        edit_entry_time = Millis_Get();
        StateMachine_SetState(STATE_EDIT_DATE);
    }
}

// ------------------------------------------------------------------
// Отображение скорости изменения
// ------------------------------------------------------------------
static void on_temp_process(void) {
    uint16_t addr;
    uint8_t cmd;
    if (APP_IR_GetCommand(&addr, &cmd) && addr == 0x414E && cmd == 0x21) {
        APP_Display_ShowSpeed(1);
    }
}

static void on_pressure_process(void) {
    uint16_t addr;
    uint8_t cmd;
    if (APP_IR_GetCommand(&addr, &cmd) && addr == 0x414E && cmd == 0x21) {
        APP_Display_ShowSpeed(2);
    }
}

static void on_humid_process(void) {
    uint16_t addr;
    uint8_t cmd;
    if (APP_IR_GetCommand(&addr, &cmd) && addr == 0x414E && cmd == 0x21) {
        APP_Display_ShowSpeed(3);
    }
}

// ------------------------------------------------------------------
// Редактирование времени
// ------------------------------------------------------------------
void inc_time_digit(void) {
    if (edit_pos > 5) return;
    const TimeDigit_t *d = &time_digits[edit_pos];

    // Вытаскиваем нужную цифру
    uint8_t digit = (*(d->val) / d->weight) % d->limit;

    // Инкрементируем с учетом лимита
    uint8_t new_digit = (digit + 1) % d->limit;

    // Записываем обратно
    *(d->val) = (*(d->val) - (digit * d->weight)) + (new_digit * d->weight);

    // Жесткий ограничитель для часов (чтобы не получилось 24, 25 и т.д.)
    if (edit_time.hour > 23) edit_time.hour = 23;
}

void dec_time_digit(void) {
    if (edit_pos > 5) return;
    const TimeDigit_t *d = &time_digits[edit_pos];

    uint8_t digit = (*(d->val) / d->weight) % d->limit;

    // Декрементируем с учетом лимита
    uint8_t new_digit = (digit == 0) ? (d->limit - 1) : (digit - 1);

    *(d->val) = (*(d->val) - (digit * d->weight)) + (new_digit * d->weight);

    if (edit_time.hour > 23) edit_time.hour = 23;
}


static void update_edit_time_display(void) {
    char buf[13];
    snprintf(buf, sizeof(buf), "Set %02d:%02d:%02d", edit_time.hour, edit_time.minute, edit_time.second);
    Display_PrintString(0, buf);
    const uint8_t map[] = {7, 6, 4, 3, 1, 0};
    for (int i = 0; i < NUM_DIGITS; i++) {
        Display_SetBrightness(i, (i == map[edit_pos]) ? 100 : 10);
    }
}

static void on_edit_time_enter(void) {
    for (int i = 0; i < NUM_DIGITS; i++) Display_SetBrightness(i, 10);
    update_edit_time_display();
}
static void on_edit_time_exit(void) {}
static void on_edit_time_process(void) {
    uint16_t addr;
    uint8_t cmd;
    if (APP_IR_GetCommand(&addr, &cmd) && addr == 0x414E) {
        switch (cmd) {
            case 0x41: edit_pos = (edit_pos + 1) % 6; update_edit_time_display(); break;
            case 0xC1: edit_pos = (edit_pos + 5) % 6; update_edit_time_display(); break;
            case 0x01: inc_time_digit(); update_edit_time_display(); break;
            case 0x81: dec_time_digit(); update_edit_time_display(); break;
            case 0x21: DS3232_SetTime(&edit_time); StateMachine_SetState(prev_state); break;
        }
    }
    if (Millis_Get() - edit_entry_time >= 60000) StateMachine_SetState(prev_state);
}

// ------------------------------------------------------------------
// Редактирование даты
// ------------------------------------------------------------------
void inc_date_digit(void) {
    if (edit_pos > 6) return;

    uint16_t weight = date_weights[edit_pos];

    if (edit_pos <= 1) { // Редактируем ДЕНЬ
        uint8_t digit = (edit_date.day / weight) % 10;
        uint8_t limit = (edit_pos == 0) ? 4 : 10; // Десятки до 3, единицы до 9
        uint8_t new_digit = (digit + 1) % limit;
        edit_date.day = (edit_date.day - (digit * weight)) + (new_digit * weight);
    }
    else if (edit_pos == 2) { // Редактируем МЕСЯЦ целиком
        edit_date.month = (edit_date.month % 12) + 1;
    }
    else { // Редактируем ГОД (разряды 3, 4, 5, 6)
        uint8_t digit = (edit_date.year / weight) % 10;
        uint8_t new_digit = (digit + 1) % 10;
        edit_date.year = (edit_date.year - (digit * weight)) + (new_digit * weight);
    }

    // Жёсткая защита от выхода за рамки реального календаря
    uint8_t max_days = get_days_in_month(edit_date.month, edit_date.year);
    if (edit_date.day > max_days) edit_date.day = max_days;
    if (edit_date.day == 0) edit_date.day = 1; // День не может быть нулевым
}

void dec_date_digit(void) {
    if (edit_pos > 6) return;

    uint16_t weight = date_weights[edit_pos];

    if (edit_pos <= 1) { // Редактируем ДЕНЬ
        uint8_t digit = (edit_date.day / weight) % 10;
        uint8_t limit = (edit_pos == 0) ? 4 : 10;
        uint8_t new_digit = (digit == 0) ? (limit - 1) : (digit - 1);
        edit_date.day = (edit_date.day - (digit * weight)) + (new_digit * weight);
    }
    else if (edit_pos == 2) { // Редактируем МЕСЯЦ целиком
        edit_date.month = (edit_date.month == 1) ? 12 : edit_date.month - 1;
    }
    else { // Редактируем ГОД (разряды 3, 4, 5, 6)
        uint8_t digit = (edit_date.year / weight) % 10;
        uint8_t new_digit = (digit == 0) ? 9 : digit - 1;
        edit_date.year = (edit_date.year - (digit * weight)) + (new_digit * weight);
    }

    // Жёсткая защита от выхода за рамки реального календаря
    uint8_t max_days = get_days_in_month(edit_date.month, edit_date.year);
    if (edit_date.day > max_days) edit_date.day = max_days;
    if (edit_date.day == 0) edit_date.day = 1;
}


static void update_edit_date_display(void) {
    const char* month_name = months[edit_date.month - 1];
    char buf[13];
    snprintf(buf, sizeof(buf), "%02d %s %04d", edit_date.day, month_name, edit_date.year);
    Display_PrintString(0, buf);
    for (int i = 0; i < NUM_DIGITS; i++) Display_SetBrightness(i, 10);
    if (edit_pos == 2) {
        Display_SetBrightness(6, 100);
        Display_SetBrightness(7, 100);
        Display_SetBrightness(8, 100);
    } else {
        const uint8_t map_single[] = {11, 10, 4, 3, 2, 1};
        int idx = (edit_pos < 2) ? edit_pos : edit_pos - 1;
        Display_SetBrightness(map_single[idx], 100);
    }
}

static void on_edit_date_enter(void) {
    for (int i = 0; i < NUM_DIGITS; i++) Display_SetBrightness(i, 10);
    update_edit_date_display();
}
static void on_edit_date_exit(void) {}
static void on_edit_date_process(void) {
    uint16_t addr;
    uint8_t cmd;
    if (APP_IR_GetCommand(&addr, &cmd) && addr == 0x414E) {
        switch (cmd) {
            case 0x41: edit_pos = (edit_pos + 1) % 7; update_edit_date_display(); break;
            case 0xC1: edit_pos = (edit_pos + 6) % 7; update_edit_date_display(); break;
            case 0x01: inc_date_digit(); update_edit_date_display(); break;
            case 0x81: dec_date_digit(); update_edit_date_display(); break;
            case 0x21: {
                DS3232_Time now;
                DS3232_GetTime(&now);
                now.date = edit_date.day;
                now.month = edit_date.month;
                now.year = edit_date.year;
                DS3232_SetTime(&now);
                StateMachine_SetState(prev_state);
                break;
            }
        }
    }
    if (Millis_Get() - edit_entry_time >= 60000) StateMachine_SetState(prev_state);
}

// ------------------------------------------------------------------
// Колбэки основных состояний
// ------------------------------------------------------------------
static void on_main_enter(void) {
    enable_sensor("BME280", false, 0);
    enable_sensor("VEML7700", false, 0);
    enable_sensor("DS3231", true, 1000);
}
static void on_main_exit(void) {}

static void on_temp_enter(void) {
    enable_sensor("BME280", true, 2000);
    enable_sensor("VEML7700", false, 0);
    enable_sensor("DS3231", false, 0);
    Display_SetBlink(0, 1);
}
static void on_temp_exit(void) {
    Display_SetBlink(0, 0);
}

static void on_pressure_enter(void) {
    enable_sensor("BME280", true, 2000);
    enable_sensor("VEML7700", false, 0);
    enable_sensor("DS3231", false, 0);
    Display_SetBlink(0, 1);
}
static void on_pressure_exit(void) {
    Display_SetBlink(0, 0);
}

static void on_hummid_enter(void) {
    enable_sensor("BME280", true, 2000);
    enable_sensor("VEML7700", false, 0);
    enable_sensor("DS3231", false, 0);
    Display_SetBlink(0, 1);
}
static void on_hummid_exit(void) {
    Display_SetBlink(0, 0);
}

static void on_date_enter(void) {
    enable_sensor("BME280", false, 0);
    enable_sensor("VEML7700", false, 0);
    enable_sensor("DS3231", true, 1000);
}
static void on_date_exit(void) {}

static void on_IR_enter(void) {}
static void on_IR_exit(void) {}

static void on_radio_enter(void) {

	enable_sensor("BME280", false, 0);
	enable_sensor("DS3231", false, 0);

	// 1. Сначала РЕАЛЬНО ИНИЦИАЛИЗИРУЕМ (загружаем патч)
    // Вызываем полный цикл включения вместо «голой» инициализации
    Radio_PowerOn();

	// 2. Читаем настройки из EEPROM
	Radio_LoadStationsFromEEPROM();

	// 3. Определяем частоту и ОБЯЗАТЕЛЬНО масштабируем ее
	uint16_t target_f;
	if (radio_mem.current_idx > 0 && radio_mem.current_idx <= 20) {
		target_f = radio_mem.freq[radio_mem.current_idx - 1];
	} else {
		target_f = (radio_mem.last_manual_freq >= 870) ? radio_mem.last_manual_freq : 887;
	}

	// Если частота пришла в старом формате (x10), переводим в формат TEF (x100)
	if (target_f < 2000) target_f *= 10;

	// 4. ТЕПЕРЬ включаем звук и ставим частоту
	Radio_SetFrequency(target_f);

	Radio_SetVolume(3); // Применяем физически

	TEF_SetCmd(48, 11, 1, 0); // Unmute

	// Для дебага:
	int16_t level = Radio_GetLevel();
	IR_DebugPrint(&ir_decoder, "TEF Tuned to %d, Level: %d\n", target_f, level);


    radio_show_message("Radio On", 2000);
    radio.mute = false;
    Radio_ResetStereoState();
    radio_freq_updated = 1;

    last_tuned = false;
    last_stereo = false;
    last_mute = false;
    last_rds_sync = false;

    // Восстанавливаем режим отображения RDS из памяти EEPROM
    radio_display_mode = radio_mem.rds_enabled;

    // Защита от мусора в памяти: если значение невалидно, сбрасываем в режим RadioText (2)
    if (radio_display_mode > 2) {
        radio_display_mode = 2;
        radio_mem.rds_enabled = 2;
        // Можно сразу подправить в EEPROM, но запишется при первом изменении
    }

    // [!] АВТОМАТИЧЕСКИЙ СТАРТ RDS НА ОСНОВЕ СОХРАНЕННОГО РЕЖИМА
    if (radio_display_mode > 0) {
        Radio_EnableRDS();
        rds_active = true;
        rds_enabled_by_user = 1;
        TaskScheduler_SetTaskEnabled("RDS", true);
        RDS_ResetRadioText();
    } else {
        rds_active = false;
        rds_enabled_by_user = 0;
        TaskScheduler_SetTaskEnabled("RDS", false);
        RDS_ResetRadioText();
    }

	// Зажигаем номера ячеек от 1 до 20
	// Цикл от 1 до 20
	for (uint8_t i = 1; i <= 20; i++) {
		if (!Radio_IsPresetEmpty(i)) {
			// Если ячейка НЕ пустая — зажигаем элемент (i - 1), так как пиктограммы идут от 0 до 19
			set_pictogram(3, i - 1);
		} else {
			// Если ячейка пустая — гасим её номер
			clear_pictogram(3, i - 1);
		}
	}

	// Управление значком ">" (остается без изменений)
	if (radio_mem.current_idx == 0) {
		set_pictogram(3, 20);
	} else {
		clear_pictogram(3, 20);
	}

	// Управление пиктограммой EX BASS
	if (Radio_IsBassOn()) {
	    set_pictogram(0, 2); //
	} else {
	    clear_pictogram(0, 2);
	}

    IR_DebugPrint(&ir_decoder, "Entered STATE_RADIO, state=%d\n", StateMachine_GetState());
    
}

static void on_radio_exit(void) {
    // 1. Показываем финальное сообщение (оно провисит 2 секунды)
    //radio_show_message("Radio Off", 2000);

    // 2. Гасим звук и выключаем чип
	Radio_Mute(true);
	Radio_PowerOff();

    // 3. Останавливаем фоновые задачи (RDS, Preview Scan)
    TaskScheduler_SetTaskEnabled("RDS", false);
    preview_active = false;

    // 4. Возвращаем датчики в строй (BME280, DS3231)
    // Твоя функция enable_sensor
    enable_sensor("BME280", true, 0);
    enable_sensor("DS3231", true, 0);

    // 5. Очищаем флаги
    radio_freq_updated = 0;
    plus10_active = false;
    radio.mute = false;
    RDS_ResetRadioText();
}


// 1. Что происходит ОДИН РАЗ при входе в режим настройки будильника
static void on_alarm_edit_enter(void) {
    alarm_edit_pos = 0;   // Фокус на выборе номера будильника
    day_select_idx = 0;   // Фокус внутри дней на Понедельнике

    // Подгружаем актуальные данные из памяти (на случай если что-то сбилось)
    Alarm_Init();

    // Сбрасываем пиктограммы, чтобы горел только номер редактируемого будильника
    //update_radio_indicators();

    IR_DebugPrint(&ir_decoder, "Entered Alarm Edit Mode\n");
}

// 2. Основной цикл работы (обработка кнопок), который мы написали ранее

static void on_alarm_edit_process(void) {
    uint16_t addr;
    uint8_t cmd;
    if (!APP_IR_GetCommand(&addr, &cmd)) return;

    IR_DebugPrint(&ir_decoder, "[ALARM_EDIT] Got cmd: 0x%02X, is_edit_mode: %d\n", cmd, alarm_db.is_edit_mode);


    Alarm_t *a = &alarm_db.list[alarm_db.current_edit_idx];

    // --- ОБЩИЕ КОМАНДЫ ---
    // Выключение будильника кнопкой 0x39 (всегда доступно)
    if (addr == 0x010E && cmd == 0x39) {
        a->days = 0;
        return;
    }

    if (!alarm_db.is_edit_mode) {
        // --- 1. РЕЖИМ ПРОСМОТРА (is_edit_mode = false) ---
        if (addr == 0x414E) {
            if (cmd == 0xC1) { // Влево (предыдущий будильник)
                alarm_db.current_edit_idx = (alarm_db.current_edit_idx > 0) ? alarm_db.current_edit_idx - 1 : 4;
            }
            else if (cmd == 0x41) { // Вправо (следующий будильник)
                alarm_db.current_edit_idx = (alarm_db.current_edit_idx + 1) % 5;
            }
            else if (cmd == 0xC9) { // Кнопка SETUP -> ВХОД В ПРАВКУ
                alarm_db.is_edit_mode = true;
                alarm_edit_pos = 1; // Начинаем с часов (H1)
                IR_DebugPrint(&ir_decoder, "[ALARM_EDIT] Entered edit mode!\n");
            }
            else if (cmd == 0x21) { // Кнопка OK -> ФИНАЛЬНОЕ СОХРАНЕНИЕ И ВЫХОД
            	if (a->freq <= 870) a->freq = Get_Radio_Current_Freq();
            	IR_DebugPrint(&ir_decoder, "FINAL SAVE. Freq in struct: %d\n", a->freq);

                // На всякий случай гарантируем выключение
                Radio_Mute(true);
                Radio_PowerOff();
                TaskScheduler_SetTaskEnabled("RDS", false);
                enable_sensor("BME280", true, 0);
                enable_sensor("DS3231", true, 0);

            	Alarm_Save(); // Запись в EEPROM
                radio_show_message("ALARM SAVED", 1500);
            	StateMachine_SetState(STATE_MAIN);
            }
        }
        // Выход по кнопкам основного пульта
        else if (addr == 0x010E && (cmd == 0x01 || cmd == 0x21)) {
            if (a->freq <= 870) a->freq = Get_Radio_Current_Freq();

            // На всякий случай гарантируем выключение
            Radio_Mute(true);
            Radio_PowerOff();
            TaskScheduler_SetTaskEnabled("RDS", false);
            enable_sensor("BME280", true, 0);
            enable_sensor("DS3231", true, 0);

            Alarm_Save();
            StateMachine_SetState(STATE_MAIN);
        }
    }
    else {
        // --- 2. РЕЖИМ РЕДАКТИРОВАНИЯ (is_edit_mode = true) ---

        // Кнопки 1-7 (Прямая установка дней)
        if (addr == 0x010E) {
            const uint8_t day_cmds[] = { 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8 };
            for (int i = 0; i < 7; i++) {
                if (cmd == day_cmds[i]) {
                    a->days ^= (1 << i);
                    if (a->days & 0x7F) a->days |= 0x80; else a->days &= ~0x80;
                    alarm_edit_pos = 5 + i; // Фокус на измененный день
                    return;
                }
            }
        }

        if (addr == 0x414E) {
            switch (cmd) {
                case 0xC1: // Влево (курсор)
                    alarm_edit_pos = (alarm_edit_pos > 1) ? alarm_edit_pos - 1 : 11;
                    break;
                case 0x41: // Вправо (курсор)
                    alarm_edit_pos = (alarm_edit_pos < 11) ? alarm_edit_pos + 1 : 1;
                    break;
                case 0x01: // Вверх (значение +)
                case 0x81: // Вниз (значение -)
                {
                    bool up = (cmd == 0x01);
                    if (alarm_edit_pos <= 4) {
                        edit_alarm_time_math(a, up);
                    } else if (alarm_edit_pos >= 5 && alarm_edit_pos <= 11) {
                        uint8_t day_idx = alarm_edit_pos - 5;
                        a->days ^= (1 << day_idx);
                        if (a->days & 0x7F) a->days |= 0x80; else a->days &= ~0x80;
                    }
                }
                break;

                case 0x0A: // Кнопка AUDIO -> Вход в выбор радиостанции для будильника
                {
                    backup_alarm_freq = a->freq; // Тут хранится старое значение из EEPROM (например, 887)

                    enable_sensor("BME280", false, 0);
                    enable_sensor("DS3231", false, 0);

                    // Если в будильнике пусто, берем дефолт 88.7 МГц.
                    // И жестко приводим К МАСШТАБУ РАДИО x100 (умножаем на 10!)
                    if (a->freq >= 870 && a->freq <= 1080) {
                        radio.freq_x100 = a->freq * 10; // Превращаем 887 в 8870
                    } else {
                        radio.freq_x100 = 8870; // Дефолт 88.70 МГц
                    }

                    // Включаем тюнер (он чисто настроится на 8870 без костылей)
                    Radio_PowerOn();

                    radio_freq_updated = 1;
                    Radio_ResetStereoState();
                    RDS_ResetRadioText();
                    TaskScheduler_SetTaskEnabled("RDS", true);

                    StateMachine_SetState(STATE_ALARM_SELECT_RADIO);
                }
                break;



                case 0x21: // OK на кресте -> ВЫХОД ИЗ ПРАВКИ В ПРОСМОТР
                    alarm_db.is_edit_mode = false;
                    break;
            }
        }
    }
}


// 3. Что происходит при выходе из режима
static void on_alarm_edit_exit(void) {
    alarm_start_tick = 0;
    is_snooze_active = false;

    // Гасим все лишние пиктограммы
    //for(int i=0; i<20; i++) clear_pictogram(3, i);
    //clear_pictogram(3, 20); // Гасим значок ">"
    alarm_db.is_edit_mode = false;

    IR_DebugPrint(&ir_decoder, "Exited Alarm Edit Mode\n");
}

static void on_alarm_select_radio_enter(void) {}
static void on_alarm_select_radio_exit(void) {}

static void on_alarm_select_radio_process(void) {
    uint16_t addr;
    uint8_t cmd;

    Alarm_t *a = &alarm_db.list[alarm_db.current_edit_idx];

    if (APP_IR_GetCommand(&addr, &cmd)) {

        // 1. ПОДТВЕРЖДЕНИЕ ЧАСТОТЫ (OK)
        if (((addr == 0x414E) && (cmd == 0x21)) || ((addr == 0x010E) && (cmd == 0x21))) {

            // Переводим обратно в масштаб x10 для структуры будильника и EEPROM (8920 -> 892)
            a->freq = radio.freq_x100 / 10;

            // 2. ЖЕЛЕЗНОЕ СОХРАНЕНИЕ: принудительно пишем обновленную структуру в EEPROM прямо сейчас!
            Alarm_Save();

            // Приводим систему в исходное состояние
            Radio_Mute(true);
            Radio_PowerOff();
            TaskScheduler_SetTaskEnabled("RDS", false);

            enable_sensor("BME280", true, 0);
            enable_sensor("DS3231", true, 0);

            radio_show_message("STATION SET", 1500);
            StateMachine_SetState(STATE_EDIT_ALARM);
            return;
        }


        // 2. ОТМЕНА ВЫБОРА ЧАСТОТЫ (Return / Вверх)
        if (((addr == 0x414E) && (cmd == 0xBA)) || ((addr == 0x010E) && (cmd == 0x01))) {

            a->freq = backup_alarm_freq; // Откатываем частоту из бэкапа

            // ПРИВОДИМ СИСТЕМУ В ИСХОДНОЕ СОСТОЯНИЕ:
            Radio_Mute(true);
            Radio_PowerOff();
            TaskScheduler_SetTaskEnabled("RDS", false);

            enable_sensor("BME280", true, 0);
            enable_sensor("DS3231", true, 0);

            StateMachine_SetState(STATE_EDIT_ALARM);    // Вернулись в меню времени без изменений
            return;
        }

        // 3. ПРОБРОС ОСТАЛЬНЫХ КОМАНД (Громкость, шаг частоты, автопоиск)
        Radio_Common_HandleCommand(addr, cmd);
    }
}

static void on_alarm_ringing_enter(void) {}

static void on_alarm_ringing_exit(void) {
    // 1. Вызываем общую "уборку" радио (выключит чип, снимет Mute, вернет датчики)
    on_radio_exit();
}

static void on_alarm_ringing_process(void) {
	Alarm_t *al = &alarm_db.list[current_ringing_alarm_idx];

	// Инициализация при первом входе
	if (alarm_start_tick == 0) {
		alarm_start_tick = Millis_Get();
		current_vol = 1;
		Radio_PowerOn();
		Radio_SetFrequency(al->freq);
		Radio_SetVolume(current_vol);
		Radio_Mute(false);
	}

    // 1. Плавное нарастание (работает само по таймеру внутри)
    Alarm_Volume_Service();

    if (Millis_Get() - alarm_start_tick > 1800000) {
    	Radio_PowerOff();
        alarm_start_tick = 0;
        is_snooze_active = false;
        radio_show_message("AUTO OFF", 2000);
        radio_msg_end = Millis_Get() + 2000;
        StateMachine_SetState(STATE_MAIN);
        return;
    }

	// ОБРАБОТКА КНОПОК
	uint16_t addr;
	uint8_t cmd;
	if (APP_IR_GetCommand(&addr, &cmd)) {
		// 1. Snooze (Любая кнопка)
		if (cmd != 0) { // Здесь можно выделить конкретную, но по ТЗ любая
			Radio_PowerOff();
			alarm_start_tick = 0; // Сброс для следующего раза
			snooze_time_ms = Millis_Get() + (120 * 1000);
			is_snooze_active = true;
			current_alarm_vol = 0;
			radio_show_message("SNOOZE 2 MIN", 2000);
		    radio_msg_end = Millis_Get() + 2000;

			StateMachine_SetState(STATE_MAIN);
		}
		if (addr == 0x010E && cmd == 0x01) {
		    // 3. Сообщение пользователю
		    radio_show_message("ALARM OFF", 2000);
		    radio_msg_end = Millis_Get() + 2000;

			StateMachine_SetState(STATE_MAIN);
			return;
		}
	}
}

// ------------------------------------------------------------------
// Функция PreviewScan
// ------------------------------------------------------------------
void Radio_StartPreviewScan(void) {
    if (Radio_GetCount() == 0) {
        radio_show_message("No Presets", 1500);
        return;
    }

    preview_active = true;
    preview_timer = Millis_Get();

    // Начинаем с первой найденной станции
    Radio_NextPreset();
    radio_show_message("Preview On", 1500);
    radio_freq_updated = 1;
    Radio_ResetStereoState();
}

void Radio_StopPreviewScan(void) {
    if (preview_active) {
        preview_active = false;
        radio_show_message("Preview Off", 1500);
        Radio_ResetStereoState();
    }
}


// ------------------------------------------------------------------
// Обновление индикаторов радио
// ------------------------------------------------------------------
void update_radio_indicators(void) {

	SystemState_t state = StateMachine_GetState();

	bool tuned = Radio_IsTuned();
	bool stereo = Radio_IsStereo();
	bool mute = Radio_IsMute();

	bool rds_sync = Radio_GetRDSStatus();

	// Управление пиктограммами для будильника
	if (state == STATE_EDIT_ALARM) {
		// Гасим все номера, кроме редактируемого
		for (uint8_t i = 1; i <= 20; i++) {
			if (i == (alarm_db.current_edit_idx + 1)) {
				// Мигаем номером активного будильника
				bool blink = (Millis_Get() / 300) % 2;
				Display_SetPictBlink(3, i - 1, blink);
				set_pictogram(3, i - 1);
			} else {
				Display_SetPictBlink(3, i - 1, false);
				clear_pictogram(3, i - 1);
			}
		}

		// Если в позиции выбора дня (5), можно зажечь значок ">" для красоты
		if (alarm_edit_pos == 5)
			set_pictogram(3, 20);
		else
			clear_pictogram(3, 20);
	} else {

		if (mute != last_mute) {
			if (mute)
				set_pictogram(2, 12);
			else
				clear_pictogram(2, 12);
			last_mute = mute;
		}
		if (rds_rt.is_ready != last_rds_sync) {
			if (rds_rt.is_ready)
				set_pictogram(1, 4);
			else
				clear_pictogram(1, 4);
			last_rds_sync = rds_rt.is_ready;
		}

		// Зажигаем номера ячеек от 1 до 20
		// Цикл от 1 до 20
		// Перебираем все 20 ячеек
		for (uint8_t i = 1; i <= 20; i++) {
			if (!Radio_IsPresetEmpty(i)) {
				// Если ячейка НЕ пустая, проверяем, не она ли сейчас играет
				if (radio_mem.current_idx == i) {
					// МИГАЕМ активной ячейкой (используем вашу новую функцию)
					Display_SetPictBlink(3, i - 1, true);
				} else {
					// Просто ЗАЖИГАЕМ остальные сохраненные станции
					set_pictogram(3, i - 1); // Обязательно останавливаем мигание, если оно было
					Display_SetPictBlink(3, i - 1, false);
				}
			} else {
				// Если ячейка пустая — полностью гасим её номер
				Display_SetPictBlink(3, i - 1, false);
				clear_pictogram(3, i - 1);
			}
		}

		// Управление значком ">" (остается без изменений)
		if (radio_mem.current_idx == 0) {
			set_pictogram(3, 20);
		} else {
			clear_pictogram(3, 20);
		}

		// Управление пиктограммой EX BASS
		if (Radio_IsBassOn()) {
			set_pictogram(0, 2);
		} else {
			clear_pictogram(0, 2);
		}

	    if (tuned) set_pictogram(1, 7);
	    else clear_pictogram(1, 7);

	    if (stereo) {
	        set_pictogram(1, 9);   // ST.
	        clear_pictogram(1, 8); // MONO
	    } else {
	        clear_pictogram(1, 9);
	        set_pictogram(1, 8);
	    }

		if (rds_enabled_by_user) {
			set_pictogram(2, 5);
		} else {
			clear_pictogram(2, 5);
		}

	    // 6. TP (Traffic Program) - Группа 1, Элемент 0
	    if (rds_tp) set_pictogram(1, 0);
	    else clear_pictogram(1, 0);

	    // 7. PTY (Program Type) - Группа 1, Элемент 3
	    // Зажигаем значок PTY, если станция передает тип программы (не 0)
	    if (rds_pty > 0) set_pictogram(1, 3);
	    else clear_pictogram(1, 3);

	    // 8. TA (Traffic Announcement) - Группа 1, Элемент 6
	    // Можно вытащить из бита 4 блока B
	    if ((rds_data[2] >> 4) & 0x01) set_pictogram(1, 6);
	    else clear_pictogram(1, 6);

	}

    // =================================================================
    // Управление пиктограммой CD-диска "(=)" при работе RadioText
    // =================================================================

    if (radio_display_mode == 2 && rds_enabled_by_user && rds_rt.is_ready) {
        // Получаем фазу анимации на основе системного времени.
        // Деление на 400 означает, что фаза будет меняться каждые 400 мс.
        // % 2 дает чередование: 0, 1, 0, 1...
        uint8_t cd_phase = (Millis_Get() / 600) % 6;

        switch (cd_phase) {
        	case 0: {
				set_pictogram(2, pict_brackets_r);   // Горят скобки (
				clear_pictogram(2, pict_equal);    // Гаснет знак =
				clear_pictogram(2, pict_brackets_l);   // Гаснет скобка  )
            }
        	break;
        	case 1: {
        		set_pictogram(2, pict_brackets_r); // Горит скобка (
				set_pictogram(2, pict_equal);     // Горит знак =
				clear_pictogram(2, pict_brackets_l);   // Гаснет скобка  )
        	}
        	break;
        	case 2:{
        		set_pictogram(2, pict_brackets_r); // Горит скобка (
				set_pictogram(2, pict_equal);     // Горит знак =
				set_pictogram(2, pict_brackets_l);   // Горяи скобка  )
        	}
        	break;
        	case 3:{
        		clear_pictogram(2, pict_brackets_r); // Гаснут скобки (
				set_pictogram(2, pict_equal);     // Горит знак =
				set_pictogram(2, pict_brackets_l);   // Горят скобка  )
        	}
        	break;
        	case 4:{
        		clear_pictogram(2, pict_brackets_r); // Гаснут скобки (
        		clear_pictogram(2, pict_equal);     // Гаснет знак =
				set_pictogram(2, pict_brackets_l);   // Горят скобки  )
        	}
        	break;
        	case 5:{
        		clear_pictogram(2, pict_brackets_r); // Гаснут скобки (
        		clear_pictogram(2, pict_equal);     // Гаснет знак =
        		clear_pictogram(2, pict_brackets_l);   // Гаснут скобки  )
        	}
        	break;
        }
        // Сбрасываем встроенный блинк драйвера, так как управляем фазами вручную
        Display_SetPictBlink(2, pict_brackets_r, false);
        Display_SetPictBlink(2, pict_brackets_l, false);
        Display_SetPictBlink(2, pict_equal, false);
    }
    if (radio_display_mode == 1) {
        clear_pictogram(2, pict_brackets_r); // Гаснут скобки (
        clear_pictogram(2, pict_equal);     // Горит знак =
        clear_pictogram(2, pict_brackets_l);   // Горят скобки  )
        // Сбрасываем встроенный блинк драйвера, так как управляем фазами вручную
        Display_SetPictBlink(2, pict_brackets_r, false);
        Display_SetPictBlink(2, pict_brackets_l, false);
        Display_SetPictBlink(2, pict_equal, false);
    }
}

// ------------------------------------------------------------------
// Таблица состояний (отформатирована для удобства чтения)
// ------------------------------------------------------------------
const StateDescriptor_t app_state_table[STATE_COUNT] = {
    [STATE_MAIN] = {
        .on_enter   = on_main_enter,
        .on_exit    = on_main_exit,
        .process    = on_main_process,
        .timeout_ms = STATE_MAIN_TIMEOUT_MS,
        .next_state = STATE_SHOW_TEMP
    },
    [STATE_SHOW_TEMP] = {
        .on_enter   = on_temp_enter,
        .on_exit    = on_temp_exit,
        .process    = on_temp_process,
        .timeout_ms = STATE_SHOW_TEMP_TIMEOUT_MS,
        .next_state = STATE_SHOW_PRESSURE
    },
    [STATE_SHOW_PRESSURE] = {
        .on_enter   = on_pressure_enter,
        .on_exit    = on_pressure_exit,
        .process    = on_pressure_process,
        .timeout_ms = STATE_SHOW_PRESS_TIMEOUT_MS,
        .next_state = STATE_SHOW_HUMMID
    },
    [STATE_SHOW_HUMMID] = {
        .on_enter   = on_hummid_enter,
        .on_exit    = on_hummid_exit,
        .process    = on_humid_process,
        .timeout_ms = STATE_SHOW_HUMID_TIMEOUT_MS,
        .next_state = STATE_SHOW_DATE
    },
    [STATE_SHOW_DATE] = {
        .on_enter   = on_date_enter,
        .on_exit    = on_date_exit,
        .process    = on_date_process,
        .timeout_ms = STATE_SHOW_DATE_TIMEOUT_MS,
        .next_state = STATE_MAIN
    },
    [STATE_SHOW_IR] = {
        .on_enter   = on_IR_enter,
        .on_exit    = on_IR_exit,
        .process    = NULL,
        .timeout_ms = STATE_SHOW_IR_TIMEOUT_MS,
        .next_state = STATE_MAIN
    },
    [STATE_EDIT_TIME] = {
        .on_enter   = on_edit_time_enter,
        .on_exit    = on_edit_time_exit,
        .process    = on_edit_time_process,
        .timeout_ms = 0,
        .next_state = STATE_MAIN
    },
    [STATE_EDIT_DATE] = {
        .on_enter   = on_edit_date_enter,
        .on_exit    = on_edit_date_exit,
        .process    = on_edit_date_process,
        .timeout_ms = 0,
        .next_state = STATE_MAIN
    },
	[STATE_EDIT_ALARM] = {
	     .on_enter = on_alarm_edit_enter,
	     .process  = on_alarm_edit_process,
	     .on_exit  = on_alarm_edit_exit,
	     .timeout_ms = 0 // Явно зануляем таймаут, чтобы не было warning
	},
	[STATE_ALARM_RINGING] = {
	     .on_enter = on_alarm_ringing_enter,
	     .process  = on_alarm_ringing_process,
	     .on_exit  = on_alarm_ringing_exit,
	     .timeout_ms = 0 // Явно зануляем таймаут, чтобы не было warning
	},
	[STATE_ALARM_SELECT_RADIO] = {
	     .on_enter = on_alarm_select_radio_enter,
	     .process  = on_alarm_select_radio_process,
	     .on_exit  = on_alarm_select_radio_exit,
	     .timeout_ms = 0 // Явно зануляем таймаут, чтобы не было warning
	},
    [STATE_RADIO] = {
        .on_enter   = on_radio_enter,
        .on_exit    = on_radio_exit,
        .process    = on_radio_process,
        .timeout_ms = 0,
        .next_state = STATE_MAIN
    }
};

void StateMachine_HandleInput(uint16_t addr, uint8_t cmd) {
    // Мы не считываем команду снова (она уже у нас в аргументах),
    // поэтому нам нужно слегка подправить логику.
    // Самый простой вариант — временно сохранить их в глобальные переменные,
    // которые on_..._process будет проверять вместо APP_IR_GetCommand.

    // НО, чтобы не переписывать все on_..._process прямо сейчас,
    // давай просто вызовем process текущего состояния,
    // предварительно положив команду обратно в "буфер" (если твоя библиотека это умеет),
    // либо просто вручную вызовем нужный обработчик.
}

void APP_States_Init(void) {
    StateMachine_Init(app_state_table, STATE_MAIN);
}

