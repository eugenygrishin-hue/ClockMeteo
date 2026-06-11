#include "app_display.h"
#include "sensor_manager.h"
#include "state_machine.h"
#include "vfd_driver.h"
#include "app_ir.h"
#include "archive.h"
#include "millis.h"
//#include "radio_rda5807m.h"
#include "tef6686.h"
#include "app_states.h"
#include "alarm.h"
#include <stdio.h>
#include <string.h>

static char time_str[16];
static const char* weekdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* months[] = {"Jan","Feb","Mar","Apr","May","June","July","Aug","Sep","Oct","Nov","Dec"};  // убрали static

static uint8_t speed_mode = 0;
static uint32_t speed_mode_start = 0;
#define SPEED_SHOW_DURATION 5000

static uint8_t anim_active = 0;          // 0 – нет анимации, 1 – вверх, 2 – вниз
static uint8_t anim_pos = 0;             // позиция знакоместа со стрелкой
static uint32_t anim_last_time = 0;
#define ANIM_INTERVAL_MS 50             // интервал сдвига (мс)

// Переменные анимации бегущей строки (теперь они железно видны для RDS_UpdateScroll)
uint8_t rt_scroll_pos = 0;
uint32_t last_scroll_time = 0;
uint16_t scroll_delay = 350;


extern char radio_temp_msg[16];
extern uint32_t radio_msg_end;
extern uint8_t radio_freq_updated;

static uint16_t last_displayed_freq = 0;
// Объявляем внешние переменные (должны быть определены в других модулях)
extern bool rds_active;          // из app_states.c
extern char rds_ps_name[9];           // из radio_rda5807m.c
int16_t smoothed_rssi;

// Запуск анимации для указанной позиции и направления
void start_animation(uint8_t pos, uint8_t direction) {
    anim_active = direction;  // 1 – вверх, 2 – вниз
    anim_pos = pos;
    anim_last_time = Millis_Get();
}

// Остановка анимации и восстановление исходного символа стрелки
void stop_animation(void) {
    if (anim_active) {
        // Восстанавливаем исходный символ стрелки (127 или 128)
        uint8_t original_char = (anim_active == 1) ? 127 : 128;
        Display_PutChar(anim_pos, original_char);
        anim_active = 0;
    }
}

void Update_Radio_Icons(uint8_t current_idx) {
    for (uint8_t i = 1; i <= 20; i++) {
        if (i == current_idx) {
            set_pictogram(3, i - 1);
            Display_SetPictBlink(3, i - 1, true);
        } else {
            // Если в памяти есть частота для этого пресета - зажигаем статично
            if (radio_mem.freq[i-1] >= 870) {
                set_pictogram(3, i - 1);
                Display_SetPictBlink(3, i - 1, false);
            } else {
                clear_pictogram(3, i - 1);
            }
        }
    }
}

static uint8_t get_trend_char(float current, float previous, float threshold) {
    if (current > previous + threshold) return 127;
    if (current < previous - threshold) return 128;
    return '=';
}

uint8_t Get_SMeter_Symbol(int16_t rssi_level) {
    // Нижний порог: все, что ниже 240 (шум эфира), считаем нулем
    if (rssi_level < 280) return ' '; // Выводим пустой символ (пробел)

    // Верхний порог: все, что выше 480 — это железобетонный сигнал
    if (rssi_level >= 480) return 133; // Максимальная шкала

    // Оставшийся диапазон полезного сигнала: 480 - 240 = 240 единиц.
    // Делим его на 5 ступеней шкалы: 280 / 5 = 48 единиц на одну ступеньку.
    uint8_t step = (rssi_level - 280) / 48;

    // Возвращаем соответствующий кастомный символ от 129 до 133
    return (129 + step);
}



static void format_speed(char *buf, size_t size, float speed, uint8_t mode) {
    if (speed > 0)
        snprintf(buf, size, "+%.1f", speed);
    else if (speed < 0)
        snprintf(buf, size, "%.1f", speed);
    else
        snprintf(buf, size, "0.0");
    size_t len = strlen(buf);
    switch (mode) {
        case 1: strncat(buf, "C/h", size - len - 1); break;
        case 2: strncat(buf, "mm/h", size - len - 1); break;
        case 3: strncat(buf, "%/h", size - len - 1); break;
    }
}

void APP_Display_ShowSpeed(uint8_t type) {
    speed_mode = type;
    speed_mode_start = Millis_Get();
}

void RDS_UpdateScroll(void) {
    // 1. Защита: если RadioText еще не накопился, выводим заглушку или имя станции
    if (!rds_rt.is_ready) {
        snprintf(time_str, 13, "WAIT RDS... "); // Ровно 12 символов, пока буфер пуст
        rt_scroll_pos = 0;
        return;
    }

    // 2. Проверяем программный таймер
    if (Millis_Get() - last_scroll_time >= scroll_delay) {
        last_scroll_time = Millis_Get();
        scroll_delay = 350; // Сбрасываем скорость на стандартную

        // 3. Вырезаем 12-символьное «окно» из длинного радиотекста
        // Спецификатор %-12.12s автоматически добьет строку пробелами справа, если текст кончился
        snprintf(time_str, 13, "%-12.12s", &rds_rt.text[rt_scroll_pos]);

        // 4. Если мы только начали (позиция 0), сделаем стартовую паузу в 1.5 секунды,
        // чтобы пользователь успел прочитать первое слово песни
        if (rt_scroll_pos == 0) {
            scroll_delay = 1500; // Следующий шаг сдвига будет только через 1.5 сек
        }

        rt_scroll_pos++;

        // 5. Условие завершения строки:
        // Если мы дошли до реального конца строки (\0) или прокрутили все 64 символа
        if (rds_rt.text[rt_scroll_pos] == '\0' || rt_scroll_pos >= 64) {
            rt_scroll_pos = 0;   // Сбрасываем на начало
            scroll_delay = 2000; // Пауза 2 секунды перед тем, как запустить строку на новый круг
        }
    }
}


void APP_Display_PrepareValues(void) {

	// --- Логика трендов ---
	float hist_t, hist_h, hist_p;

	// Для температуры и влажности берем данные 1 час назад
	bool has_1h = Archive_GetEntryAt(1, &hist_t, &hist_h, NULL);
	// Для давления берем данные 3 часа назад (метео-стандарт)
	bool has_3h = Archive_GetEntryAt(3, NULL, NULL, &hist_p);

	// ----- Режим показа скорости -----
	if (speed_mode != 0) {
		if (Millis_Get() - speed_mode_start >= SPEED_SHOW_DURATION) {
			speed_mode = 0;
		} else {
			if (Archive_GetCount() < 2) {
				Display_PrintString(0, "No data");
				return;
			}
			float speed_raw = Archive_GetSpeedRaw(speed_mode);
			float speed = 0.0f;
			int valid = 1;
			switch (speed_mode) {
			case 1: // температура
				speed = speed_raw;
				if (speed > 10.0f || speed < -10.0f)
					valid = 0;
				break;
			case 2: // давление
				speed = speed_raw * 7.50062f;
				if (speed > 20.0f || speed < -20.0f)
					valid = 0;
				break;
			case 3: // влажность
				speed = speed_raw;
				if (speed > 20.0f || speed < -20.0f)
					valid = 0;
				break;
			}
			char buf[13];
			if (!valid)
				snprintf(buf, sizeof(buf), "---");
			else
				format_speed(buf, sizeof(buf), speed, speed_mode);
			Display_PrintString(0, buf);
			return;
		}
	}

	// ----- Обычный режим -----
	SystemState_t state = StateMachine_GetState();
	if (state == STATE_EDIT_TIME || state == STATE_EDIT_DATE)
		return;

	for (int i = 0; i < NUM_DIGITS; i++)
		Display_SetBrightness(i, 100);

	char *out_str = time_str;
	Sensor_t *s;

	// Получение архивных данных (если есть две записи)
	float last_t, last_h, last_p, prev_t, prev_h, prev_p;
	//uint8_t has_archive = (Archive_GetCount() >= 2)
	//		&& Archive_GetLastEntry(&last_t, &last_h, &last_p)
	//		&& Archive_GetPrevEntry(&prev_t, &prev_h, &prev_p);

	switch (state) {
	case STATE_MAIN:
		if (radio_msg_end > Millis_Get() && radio_temp_msg[0] != '\0') {
			snprintf(time_str, sizeof(time_str), "%.12s", radio_temp_msg);
		} else {
			s = SensorManager_GetSensor("DS3231");
			if (s && s->status == SENSOR_OK) {
				uint8_t w = (uint8_t) s->get_value(s->instance, 3);
				uint8_t h = (uint8_t) s->get_value(s->instance, 2);
				uint8_t m = (uint8_t) s->get_value(s->instance, 1);
				uint8_t sec = (uint8_t) s->get_value(s->instance, 0);
				//w = (w==0) ? 7 : w;
				if (w >= 0 && w <= 7) {
					if (sec % 2 == 0)
						snprintf(time_str, sizeof(time_str),
								"%s %02d:%02d:%02d", weekdays[w], h, m, sec);
					else
						snprintf(time_str, sizeof(time_str),
								"%s %02d %02d %02d", weekdays[w], h, m, sec);
				} else
					out_str = "WRTC Err";
			} else
				out_str = "Sens RTC Err";
		}
		break;

	case STATE_SHOW_TEMP:
		s = SensorManager_GetSensor("BME280");
		if (s && s->status == SENSOR_OK) {
			float t = s->get_value(s->instance, 0);
			uint8_t trend = '=';
			// Порог 0.25°C за час - это реальное изменение, остальное шум
			if (has_1h) {
				if (t > hist_t + 0.25f)
					trend = 127;      // Вверх
				else if (t < hist_t - 0.25f)
					trend = 128; // Вниз
			}
			snprintf(time_str, sizeof(time_str), "Tmp:%.2fC %c", t, trend);
		} else
			out_str = "Temp Err";
		break;

	case STATE_SHOW_PRESSURE:
		s = SensorManager_GetSensor("BME280");
		if (s && s->status == SENSOR_OK) {
			float p_now = s->get_value(s->instance, 2) * 0.750062f; // в мм рт.ст.
			uint8_t trend = '=';
			if (has_3h) {
				float p_old = hist_p * 0.750062f;
				// Порог 0.5 мм рт.ст за 3 часа - классика прогноза
				if (p_now > p_old + 0.5f)
					trend = 127;
				else if (p_now < p_old - 0.5f)
					trend = 128;
			}
			snprintf(time_str, sizeof(time_str), " %.1fmmHg %c", p_now,
					trend);
		} else
			out_str = "Pres Err";
		break;

	case STATE_SHOW_HUMMID:
		s = SensorManager_GetSensor("BME280");
		if (s && s->status == SENSOR_OK) {
			float h = s->get_value(s->instance, 1);
			uint8_t trend = '=';
			// Влажность шумная, ставим порог 2% за час
			if (has_1h) {
				if (h > hist_h + 2.0f)
					trend = 127;
				else if (h < hist_h - 2.0f)
					trend = 128;
			}
			snprintf(time_str, sizeof(time_str), "Humm:%.1f%% %c", h, trend);
		} else
			out_str = "Hummid Err";
		break;

	case STATE_SHOW_DATE:
		s = SensorManager_GetSensor("DS3231");
		if (s && s->status == SENSOR_OK) {
			uint8_t d = (uint8_t) s->get_value(s->instance, 4);
			uint8_t m = (uint8_t) s->get_value(s->instance, 5);
			uint16_t y = (uint16_t) s->get_value(s->instance, 6);
			snprintf(time_str, sizeof(time_str), "%d %s %d", d, months[m - 1],
					y);
		} else
			out_str = "Date Err";
		break;

	case STATE_SHOW_IR:
		snprintf(time_str, sizeof(time_str), "c=%02x a=%04x",
				(uint8_t) APP_IR_GetLastCommand(),
				(uint16_t) APP_IR_GetLastAddress());
		break;

    case STATE_RADIO: {
        if (radio_msg_end > Millis_Get() && radio_temp_msg[0] != '\0') {
            snprintf(time_str, sizeof(time_str), "%s", radio_temp_msg);
        }
        // ЕСЛИ РАДИОТЕКСТ ГОТОВ — КРУТИМ БЕГУЩУЮ СТРОКУ
        else if (radio_display_mode == 2 && rds_active && rds_rt.is_ready) {
            RDS_UpdateScroll(); // Функция сама запишет 12 символов в time_str
        }
        // ЕСЛИ РАДИОТЕКСТА НЕТ, НО ЕСТЬ ИМЯ СТАНЦИИ (PS)
        else if (radio_display_mode == 1 && rds_active && Radio_HasPSData()) {
            snprintf(time_str, sizeof(time_str), "RDS:%8s", rds_ps_name);
        }
        // ЕСЛИ RDS ВООБЩЕ НЕТ — ВЫВОДИМ ЧАСТОТУ И НАШ КРАСИВЫЙ S-METER
        else {
            if (radio_freq_updated || last_displayed_freq == 0) {
                last_displayed_freq = radio.freq_x100;
                radio_freq_updated = 0;
            }
            Radio_SetSeekThreshold(8);

            smoothed_rssi = ((smoothed_rssi * 3) + Radio_GetLevel()) / 4;
            char smeter_char = is_seeking ? ' ' : Get_SMeter_Symbol(smoothed_rssi);

            if (is_seeking) {
                snprintf(time_str, 13, "%3d.%02d SCAN ",
                         radio.freq_x100 / 100,
                         radio.freq_x100 % 100);
            } else {
                snprintf(time_str, 13, "%3d.%02d     %c",
                         radio.freq_x100 / 100,
                         radio.freq_x100 % 100,
                         smeter_char);
            }
        }
        break;
    }



	case STATE_EDIT_ALARM: {
	    Alarm_t *a = &alarm_db.list[alarm_db.current_edit_idx];
	    uint32_t now = Millis_Get();
	    //bool blink = (now / 300) % 2;

	    // --- 1. ФОРМИРОВАНИЕ СТРОКИ ---
	    if (!(a->days & 0x80) && !alarm_db.is_edit_mode) {
	        // 1.2: Если выключен и мы в режиме просмотра - показываем прочерки
	        strncpy(time_str, "--:--.......", 13);
	    } else {
	        // Формируем HH:MMMTWTFSS (12 символов)
	        snprintf(time_str, 6, "%02d:%02d", a->hour, a->minute);
	        const char *days_chars = "MTWTFSS";
	        for (int i = 0; i < 7; i++) {
	            time_str[5 + i] = (a->days & (1 << i)) ? days_chars[i] : '.';
	        }
	    }

	    // --- 2. ЛОГИКА ЯРКОСТИ (ЗЕРКАЛЬНАЯ) ---
	    // 11 10  9  8  7  6  5  4  3  2  1  0  (Индексы VFD)
	    //  H  H  :  M  M  M  T  W  T  F  S  S  (Символы)

	    int focus_vfd_idx = -1;
	    if (alarm_db.is_edit_mode) {
	    	if (alarm_edit_pos <= 4) {
	    	    if (alarm_edit_pos == 1) focus_vfd_idx = 11;
	    	    if (alarm_edit_pos == 2) focus_vfd_idx = 10;
	    	    if (alarm_edit_pos == 3) focus_vfd_idx = 8;
	    	    if (alarm_edit_pos == 4) focus_vfd_idx = 7;
	    	} else {
	    	    // Позиции 5..11 соответствуют индексам дней 0..6
	    	    int day_idx = alarm_edit_pos - 5;
	    	    focus_vfd_idx = 6 - day_idx; // Зеркально: Пн(индекс 6) ... Вс(индекс 0)
	    	}
	    }

	    for (int i = 0; i < 12; i++) {
	        if (alarm_db.is_edit_mode && i == focus_vfd_idx) {
	            Display_SetBrightness(i, 100); // 1.3.1 и 1.3.3: Активный 100%
	        } else {
	            Display_SetBrightness(i, 10);  // 1.1.1: Неактивные 10%
	        }
	    }

	    // --- 3. ПИКТОГРАММЫ НОМЕРОВ (1.1) ---
	    // 1. Управление пиктограммами 1-5 (Требование 1.1)
	    for (uint8_t i = 1; i <= 5; i++) {
	        // Индекс в группе 3 для цифр 1-5 это i-1
	        uint8_t pict_idx = i - 1;

	        if (i == (alarm_db.current_edit_idx + 1)) {
	            // ТЕКУЩИЙ будильник: включаем мигание (через аппаратный флаг драйвера)
	            set_pictogram(3, pict_idx);
	            Display_SetPictBlink(3, pict_idx, true);
	        } else {
	            // ОСТАЛЬНЫЕ 1-5: просто горят статично
	            set_pictogram(3, pict_idx);
	            Display_SetPictBlink(3, pict_idx, false);
	        }
	    }

	    // ГАСИМ остальные цифры (6-20), чтобы не мешали в режиме будильника
	    for (uint8_t i = 6; i <= 20; i++) {
	        clear_pictogram(3, i - 1);
	        Display_SetPictBlink(3, i - 1, false);
	    }

	    // 2. Управление значком ">" (Индекс 20 в группе 3)
	    // Можно использовать его как индикатор режима редактирования (is_edit_mode)
	    if (alarm_db.is_edit_mode) {
	        set_pictogram(3, 20); // Горит значок ">", когда мы "внутри" настроек
	    } else {
	        clear_pictogram(3, 20);
	    }
	    break;
	}

    case STATE_ALARM_SELECT_RADIO: {
        Radio_SetSeekThreshold(8);
        smoothed_rssi = ((smoothed_rssi * 3) + Radio_GetLevel()) / 4;
        char smeter_char = is_seeking ? ' ' : Get_SMeter_Symbol(smoothed_rssi);

        // Защита масштаба: принудительно приводим к x100, если частота улетела выше 10800
        uint16_t display_freq = radio.freq_x100;
        if (display_freq > 10800) {
            display_freq /= 10;
        }

        if (is_seeking) {
            snprintf(time_str, 13, "AL %3d.%02d SC",
                     display_freq / 100,
                     display_freq % 100);
        } else {
            snprintf(time_str, 13, "AL %3d.%02d  %c",
                     display_freq / 100,
                     display_freq % 100,
                     smeter_char);
        }
        break;
    }

	case STATE_ALARM_RINGING: {
	    bool blink = (Millis_Get() / 200) % 2;
	    if (blink) {
	        strncpy(time_str, "  ALARM!!!  ", 13);
	    } else {
	        // Показываем время будильника
	        Alarm_t *al = &alarm_db.list[current_ringing_alarm_idx];
	        snprintf(time_str, 13, "WAKE %02d:%02d", al->hour, al->minute);
	    }
	    for (int i = 0; i < 12; i++) Display_SetBrightness(i, 100);
	    break;
	}

	default:
		out_str = "Unknown";
		break;
	}
	Display_PrintString(0, out_str);
}

void VFD_Icons_ClearAll(void) {
    for (int g = 0; g < 4; g++) {
        for (int p = 0; p <= 20; p++) {
            clear_pictogram(g, p);
            Display_SetPictBlink(g, p, false);
        }
    }
}

void VFD_Icons_ApplyProfile(SystemState_t state) {
    // 1. ПОЛНАЯ ОЧИСТКА: перед включением нового режима гасим ВСЁ
    VFD_Icons_ClearAll();

    // Сбрасываем блинки для всех критических иконок, чтобы не залипали
    Display_SetPictBlink(0, 13, false); // Часы
    Display_SetPictBlink(0, 12, false); // Рамка

    switch (state) {
        case STATE_MAIN:
            if (Alarm_HasAnyActive()) set_pictogram(0, 13); // Часы ⏰
            if (get_is_snooze_active()) set_pictogram(0, 16); // Sleep
            break;

        case STATE_RADIO:
        case STATE_ALARM_SELECT_RADIO:
            // Вызываем твою готовую функцию индикаторов!
            // Она сама проверит tuned, stereo, mute, номера ячеек 1-20 и закрутит CD-диск.
            update_radio_indicators();
            break;

        case STATE_EDIT_ALARM:
            set_pictogram(0, 12); // Рамка
            set_pictogram(0, 13); // Часы
            set_pictogram(0, 15); // Daily

            // Вызываем локальное обновление для режима настройки будильника
            // (мигание номера редактируемой ячейки, значок ">" и т.д.)
            //update_alarm_indicators();
            break;

        case STATE_ALARM_RINGING:
            set_pictogram(0, 13); // Часы мигают
            Display_SetPictBlink(0, 13, true);
            set_pictogram(0, 12); // Рамка мигает
            Display_SetPictBlink(0, 12, true);
            break;
    }
}




void VFD_Notify_Save(void) {
    set_pictogram(0, 13); // Маленький Rec внутри рамки вспыхивает
    // Погасить его можно через 500мс таймером или оставить до смены стейта
}


void APP_Display_Update(void)
{
    Display_Update();   // физическое обновление VFD
}

