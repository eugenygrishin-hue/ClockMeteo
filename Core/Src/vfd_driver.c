#include "vfd_driver.h"
#include "ir_nec.h"
#include "main.h"          // для HAL, hspi1, __NOP()
#include "board.h"         // для VFD_LE_PORT, VFD_LE_PIN, VFD_STR_PORT, VFD_STR_PIN
#include <string.h>
#include <stdio.h>

// ======================= Локальные данные =======================
static uint8_t display_buffer[NUM_DIGITS][7];   // 12 знакомест по 7 строк
static uint8_t pict_data[NUM_PICT_GROUPS][6];   // 4 группы по 6 байт анодов
static uint8_t pict_blink[NUM_PICT_GROUPS][6];  // флаги мигания для каждой пиктограммы (бит = 1 если мигает)
static volatile uint8_t current_pos = 0;        // текущий выводимый элемент (0..15)
static uint8_t spi_data[8];                     // буфер для SPI

extern IR_NEC_Decoder ir_decoder;

// Массивы для управления яркостью и миганием символов
static uint8_t char_brightness[NUM_DIGITS];     // 0..100
static uint8_t char_blink[NUM_DIGITS];          // 0/1
static uint8_t blink_phase = 0;                 // текущая фаза мигания
static uint16_t blink_counter = 0;              // счётчик для смены фазы (1 мс тики)

// Яркость для групп пиктограмм
static uint8_t pict_brightness[NUM_PICT_GROUPS];   // 0..100

// ======================= Шрифт 5x7 ==============================
static const uint8_t font[102][5] = {
    // 32 (space)
    {0x00, 0x00, 0x00, 0x00, 0x00},
    // 33 '!'
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    // 34 '"'
    {0x00, 0x07, 0x00, 0x07, 0x00},
    // 35 '#'
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    // 36 '$'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    // 37 '%'
    {0x23, 0x13, 0x08, 0x64, 0x62},
    // 38 '&'
    {0x36, 0x49, 0x55, 0x22, 0x50},
    // 39 '\''
    {0x00, 0x05, 0x03, 0x00, 0x00},
    // 40 '('
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    // 41 ')'
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    // 42 '*'
    {0x14, 0x08, 0x3E, 0x08, 0x14},
    // 43 '+'
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    // 44 ','
    {0x00, 0x50, 0x30, 0x00, 0x00},
    // 45 '-'
    {0x08, 0x08, 0x08, 0x08, 0x08},
    // 46 '.'
    {0x00, 0x60, 0x60, 0x00, 0x00},
    // 47 '/'
    {0x20, 0x10, 0x08, 0x04, 0x02},
    // 48 '0'
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    // 49 '1'
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    // 50 '2'
    {0x42, 0x61, 0x51, 0x49, 0x46},
    // 51 '3'
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    // 52 '4'
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    // 53 '5'
    {0x27, 0x45, 0x45, 0x45, 0x39},
    // 54 '6'
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    // 55 '7'
    {0x01, 0x71, 0x09, 0x05, 0x03},
    // 56 '8'
    {0x36, 0x49, 0x49, 0x49, 0x36},
    // 57 '9'
    {0x06, 0x49, 0x49, 0x29, 0x1E},
    // 58 ':'
    {0x00, 0x36, 0x36, 0x00, 0x00},
    // 59 ';'
    {0x00, 0x56, 0x36, 0x00, 0x00},
    // 60 '<'
    {0x08, 0x14, 0x22, 0x41, 0x00},
    // 61 '='
    {0x14, 0x14, 0x14, 0x14, 0x14},
    // 62 '>'
    {0x00, 0x41, 0x22, 0x14, 0x08},
    // 63 '?'
    {0x02, 0x01, 0x51, 0x09, 0x06},
    // 64 '@'
    {0x32, 0x49, 0x79, 0x41, 0x3E},
    // 65 'A'
    {0x7E, 0x11, 0x11, 0x11, 0x7E},
    // 66 'B'
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    // 67 'C'
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    // 68 'D'
    {0x7F, 0x41, 0x41, 0x22, 0x1C},
    // 69 'E'
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    // 70 'F'
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    // 71 'G'
    {0x3E, 0x41, 0x49, 0x49, 0x7A},
    // 72 'H'
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    // 73 'I'
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    // 74 'J'
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    // 75 'K'
	{0x7F, 0x08, 0x14, 0x22, 0x41},
    // 76 'L'
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    // 77 'M'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    // 78 'N'
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    // 79 'O'
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    // 80 'P'
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    // 81 'Q'
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    // 82 'R'
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    // 83 'S'
    {0x46, 0x49, 0x49, 0x49, 0x31},
    // 84 'T'
    {0x01, 0x01, 0x7F, 0x01, 0x01},
    // 85 'U'
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    // 86 'V'
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    // 87 'W'
    {0x7F, 0x20, 0x18, 0x20, 0x7F},
    // 88 'X'
    {0x63, 0x14, 0x08, 0x14, 0x63},
    // 89 'Y'
    {0x07, 0x08, 0x70, 0x08, 0x07},
    // 90 'Z'
    {0x61, 0x51, 0x49, 0x45, 0x43},
    // 91 '['
    {0x00, 0x7F, 0x41, 0x41, 0x00},
    // 92 '\'
    {0x02, 0x04, 0x08, 0x10, 0x20},
    // 93 ']'
    {0x00, 0x41, 0x41, 0x7F, 0x00},
    // 94 '^'
    {0x04, 0x02, 0x01, 0x02, 0x04},
    // 95 '_'
    {0x40, 0x40, 0x40, 0x40, 0x40},
    // 96 '`'
    {0x00, 0x01, 0x02, 0x04, 0x00},
    // 97 'a'
    {0x20, 0x54, 0x54, 0x54, 0x78},
    // 98 'b'
    {0x7F, 0x48, 0x44, 0x44, 0x38},
    // 99 'c'
    {0x38, 0x44, 0x44, 0x44, 0x20},
    // 100 'd'
    {0x38, 0x44, 0x44, 0x48, 0x7F},
    // 101 'e'
    {0x38, 0x54, 0x54, 0x54, 0x18},
    // 102 'f'
    {0x08, 0x7E, 0x09, 0x01, 0x02},
    // 103 'g'
    {0x0C, 0x52, 0x52, 0x52, 0x3E},
    // 104 'h'
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    // 105 'i'
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    // 106 'j'
    {0x20, 0x40, 0x44, 0x3D, 0x00},
    // 107 'k'
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    // 108 'l'
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    // 109 'm'
    {0x7C, 0x04, 0x18, 0x04, 0x78},
    // 110 'n'
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    // 111 'o'
    {0x38, 0x44, 0x44, 0x44, 0x38},
    // 112 'p'
    {0x7C, 0x14, 0x14, 0x14, 0x08},
    // 113 'q'
    {0x08, 0x14, 0x14, 0x18, 0x7C},
    // 114 'r'
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    // 115 's'
    {0x48, 0x54, 0x54, 0x54, 0x20},
    // 116 't'
    {0x04, 0x3F, 0x44, 0x40, 0x20},
    // 117 'u'
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    // 118 'v'
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    // 119 'w'
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    // 120 'x'
    {0x44, 0x28, 0x10, 0x28, 0x44},
    // 121 'y'
    {0x0C, 0x50, 0x50, 0x50, 0x3C},
    // 122 'z'
    {0x44, 0x64, 0x54, 0x4C, 0x44},
    // 123 '{'
    {0x00, 0x08, 0x36, 0x41, 0x00},
    // 124 '|'
    {0x00, 0x00, 0x7F, 0x00, 0x00},
    // 125 '}'
    {0x00, 0x41, 0x36, 0x08, 0x00},
    // 126 '~'
    {0x08, 0x08, 0x2A, 0x1C, 0x08},
    // ASCII 127: стрелка вверх
    {0x04, 0x06, 0x7F, 0x06, 0x04},
    // ASCII 128: стрелка вниз
    {0x10, 0x30, 0x7F, 0x30, 0x10},
    //ASCII 129 (Только первая колонка, высота 3 точки):
    {0x70, 0x00, 0x00, 0x00, 0x00},
	//ASCII 130 (1-я колонка 3 точки, 2-я — 4 точки):
	{0x70, 0x78, 0x00, 0x00, 0x00},
	//ASCII 131 (Добавляем 3-ю колонку высотой 5 точек):
	{0x70, 0x78, 0x7C, 0x00, 0x00},
	//ASCII 132 (Добавляем 4-ю колонку высотой 6 точек):
	{0x70, 0x78, 0x7C, 0x7E, 0x00},
	//ASCII 133 (Твой финальный символ, горят все 5 колонок с нарастанием):
	//Согласно твоей матрице (где нижние 3 строки горят везде, а верх идет лесенкой):
	{0x70, 0x78, 0x7C, 0x7E, 0x7F}
};

// ======================= Карта пиктограмм =======================
static const uint8_t pict_map[4][48] = {
    // Группа 0
    { 27, 28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11 },
    // Группа 1
    { 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11 },
    // Группа 2
    { 28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11 },
    // Группа 3
    { 39, 24, 25, 26, 27, 28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11 }
};

// ======================= Вспомогательные функции =======================
static uint8_t logical_to_physical(uint8_t pos) {
    return 11 - pos;  // 12 знакомест: 0..11 (слева направо -> справа налево)
}

// ======================= Публичные функции =======================

void Display_Init(void) {
    for (int i = 0; i < NUM_DIGITS; i++) {
        for (int row = 0; row < 7; row++) {
            display_buffer[i][row] = 0;
        }
        char_brightness[i] = 100;
        char_blink[i] = 0;
    }
    for (int g = 0; g < NUM_PICT_GROUPS; g++) {
        for (int b = 0; b < 6; b++) {
            pict_data[g][b] = 0;
            pict_blink[g][b] = 0;
        }
        pict_brightness[g] = 100;
    }
    current_pos = 0;
    blink_phase = 0;
    blink_counter = 0;
}

void Display_ArrowUp(uint8_t pos) {
    Display_PutChar(pos, 127);
}

void Display_ArrowDown(uint8_t pos) {
    Display_PutChar(pos, 128);
}

void Display_PutChar(uint8_t pos, uint8_t ch) {
    if (pos >= NUM_DIGITS) return;
    uint8_t phys = logical_to_physical(pos);
    if (ch < 32 || ch > 135) ch = 32;
    uint8_t idx = ch - 32;

    for (int row = 0; row < 7; row++) {
        uint8_t r = 0;
        for (int col = 0; col < 5; col++) {
            if (font[idx][col] & (1 << row)) {
                r |= (1 << (4 - col));   // отражение по горизонтали
            }
        }
        display_buffer[phys][6 - row] = r & 0x1F;  // вертикальное отражение
    }
}

void Display_PrintString(uint8_t start_pos, char *str) {
    for (int i = 0; i < NUM_DIGITS; i++) {
        Display_PutChar(i, ' ');
    }
    uint8_t i = 0;
    while (*str && (start_pos + i) < NUM_DIGITS) {
        Display_PutChar(start_pos + i, (uint8_t)(*str));
        str++;
        i++;
    }
}

void Display_Update(void) {
    // Строб начала передачи кадра
    HAL_GPIO_WritePin(VFD_STR_PORT, VFD_STR_PIN, GPIO_PIN_SET);

    // Отправляем ровно 8 готовых байт. Таймаут 10мс вместо HAL_MAX_DELAY защитит от зависания!
    HAL_SPI_Transmit(&hspi1, vfd_tx_buffer[current_pos], 8, 10);

    // Защелкиваем данные в физические регистры VFD
    HAL_GPIO_WritePin(VFD_LE_PORT, VFD_LE_PIN, GPIO_PIN_SET);

    // Расчет задержки яркости (ШИМ) — выполняется мгновенно без циклов
    uint8_t pos = current_pos;
    uint16_t brightness_delay = 0;
    if (pos < 12) {
        if (char_brightness[pos] < 100) {
            brightness_delay = (100 - char_brightness[pos]) * 10;
        }
    } else {
        if (pict_brightness[pos - 12] < 100) {
            brightness_delay = (100 - pict_brightness[pos - 12]) * 10;
        }
    }

    // Программный ШИМ задержки (если яркость меньше 100%)
    if (brightness_delay > 0) {
        for (volatile int i = 0; i < brightness_delay; i++) {
            __NOP();
        }
    }

    // Сбрасываем стробы управления в исходное состояние
    HAL_GPIO_WritePin(VFD_LE_PORT, VFD_LE_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(VFD_STR_PORT, VFD_STR_PIN, GPIO_PIN_RESET);

    // Переходим к следующему знакоместу (0 -> 1 -> ... -> 15 -> 0)
    current_pos = (current_pos + 1) % 16;

    blink_counter++;
    if (blink_counter >= 4000) {
        blink_phase ^= 1;
        blink_counter = 0;
    }
}

/*
void Display_Update(void) {
    HAL_GPIO_WritePin(VFD_STR_PORT, VFD_STR_PIN, GPIO_PIN_SET);

    uint8_t grid_low = 0;
    uint8_t grid_high = 0;
    uint16_t brightness_delay = 0;

    if (current_pos < 12) {
        uint8_t pos = current_pos;
        uint8_t visible = 1;
        if (char_blink[pos]) visible = blink_phase;

        uint64_t data_bits = 0;
        if (visible) {
            for (int row = 0; row < 7; row++) {
                data_bits |= ((uint64_t)(display_buffer[pos][row] & 0x1F) << (row * 5));
            }
        }
        uint64_t full = data_bits << 1;

        // Формируем 6 байт анодных данных прямо в spi_data
        spi_data[0] = (full >> 40) & 0xFF;
        spi_data[1] = (full >> 32) & 0xFF;
        spi_data[2] = (full >> 24) & 0xFF;
        spi_data[3] = (full >> 16) & 0xFF;
        spi_data[4] = (full >> 8) & 0xFF;
        spi_data[5] = full & 0xFF;

        // Сетка знакоместа
        if (pos < 5) {
            grid_high = 1 << (pos + 3);
        } else {
            grid_low = 1 << (pos - 5);
        }

        brightness_delay = 0;
        if (visible && char_brightness[pos] < 100) {
            brightness_delay = (100 - char_brightness[pos]) * 10;
        }
    } else {
        uint8_t group = current_pos - 12;
        // Временный буфер для пиктограмм с учётом мигания
        uint8_t temp_pict[6];
        for (int byte = 0; byte < 6; byte++) {
            uint8_t data_byte = pict_data[group][byte];
            uint8_t blink_byte = pict_blink[group][byte];
            uint8_t result_byte = 0;
            for (int bit = 0; bit < 8; bit++) {
                uint8_t mask = (1 << bit);
                if (data_byte & mask) {
                    if (blink_byte & mask) {
                        if (blink_phase) result_byte |= mask;
                    } else {
                        result_byte |= mask;
                    }
                }
            }
            temp_pict[byte] = result_byte;
        }
        // Копируем во spi_data
        memcpy(spi_data, temp_pict, 6);

        brightness_delay = 0;
        // Яркость пиктограмм
        if (pict_brightness[group] < 100) {
            brightness_delay = (100 - pict_brightness[group]) * 10;
        }

        switch (group) {
            case 0: grid_high = 1 << 0; break;
            case 1: grid_high = 1 << 1; break;
            case 2: grid_high = 1 << 2; break;
            case 3: grid_low  = 1 << 7; break;
        }
    }

    // Баланс сетки – больше ничего не копируем, spi_data уже заполнен
    spi_data[6] = grid_low;
    spi_data[7] = grid_high;

    //static int cnt = 0;
    //if (cnt++ % 1000 == 0) {
    //    IR_DebugPrint(&ir_decoder, "Display_Update: pos=%d\n", current_pos);
    //}

    HAL_SPI_Transmit(&hspi1, spi_data, 8, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(VFD_LE_PORT, VFD_LE_PIN, GPIO_PIN_SET);

    if (brightness_delay > 0) {
        for (volatile int i = 0; i < brightness_delay; i++) __NOP();
    }

    HAL_GPIO_WritePin(VFD_LE_PORT, VFD_LE_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(VFD_STR_PORT, VFD_STR_PIN, GPIO_PIN_RESET);

    current_pos = (current_pos + 1) % 16;

    // Мигание (1 Гц) – порог 20000 мс? Опечатка: должно быть 500 мс, а не 20000.
    // При частоте вызова 1 кГц 20000 тиков = 20 секунд! Исправить:
    blink_counter++;
    if (blink_counter >= 4000) {   // 500 мс
        blink_phase ^= 1;
        blink_counter = 0;
    }
}
*/

void set_pictogram(uint8_t group, uint8_t pict_num) {
    if (group >= NUM_PICT_GROUPS || pict_num >= 48) return;
    uint8_t bit = pict_map[group][pict_num];
    if (bit == 0xFF) return;
    uint8_t byte_idx = bit / 8;
    uint8_t bit_in_byte = bit % 8;
    __disable_irq();
    pict_data[group][byte_idx] |= (1 << bit_in_byte);
    __enable_irq();
}

void clear_pictogram(uint8_t group, uint8_t pict_num) {
    if (group >= NUM_PICT_GROUPS || pict_num >= 48) return;
    uint8_t bit = pict_map[group][pict_num];
    if (bit == 0xFF) return;
    uint8_t byte_idx = bit / 8;
    uint8_t bit_in_byte = bit % 8;
    __disable_irq();
    pict_data[group][byte_idx] &= ~(1 << bit_in_byte);
    __enable_irq();
}

// Управление миганием отдельной пиктограммы
void Display_SetPictBlink(uint8_t group, uint8_t pict_num, uint8_t blink) {
    if (group >= NUM_PICT_GROUPS || pict_num >= 48) return;
    uint8_t bit = pict_map[group][pict_num];
    if (bit == 0xFF) return;
    uint8_t byte_idx = bit / 8;
    uint8_t bit_in_byte = bit % 8;
    __disable_irq();
    if (blink)
        pict_blink[group][byte_idx] |= (1 << bit_in_byte);
    else
        pict_blink[group][byte_idx] &= ~(1 << bit_in_byte);
    __enable_irq();
}

// Управление яркостью группы пиктограмм
void Display_SetPictBrightness(uint8_t group, uint8_t brightness) {
    if (group >= NUM_PICT_GROUPS) return;
    if (brightness > 100) brightness = 100;
    __disable_irq();
    pict_brightness[group] = brightness;
    __enable_irq();
}

void static_display(uint64_t mask) {
    uint8_t data[8];
    data[0] = (mask >> 56) & 0xFF;
    data[1] = (mask >> 48) & 0xFF;
    data[2] = (mask >> 40) & 0xFF;
    data[3] = (mask >> 32) & 0xFF;
    data[4] = (mask >> 24) & 0xFF;
    data[5] = (mask >> 16) & 0xFF;
    data[6] = (mask >> 8) & 0xFF;
    data[7] = (mask >> 0) & 0xFF;

    HAL_GPIO_WritePin(VFD_STR_PORT, VFD_STR_PIN, GPIO_PIN_SET);
    HAL_SPI_Transmit(&hspi1, data, 8, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(VFD_LE_PORT, VFD_LE_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(VFD_LE_PORT, VFD_LE_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(VFD_STR_PORT, VFD_STR_PIN, GPIO_PIN_RESET);
}

void Display_SetBrightness(uint8_t pos, uint8_t brightness) {
    if (pos >= NUM_DIGITS) return;
    if (brightness > 100) brightness = 100;
    __disable_irq();
    char_brightness[pos] = brightness;
    __enable_irq();
}

void Display_SetBlink(uint8_t pos, uint8_t blink) {
    if (pos >= NUM_DIGITS) return;
    __disable_irq();
    char_blink[pos] = blink ? 1 : 0;
    __enable_irq();
}

// Сдвиг строк в знакоместе
void Display_ShiftRows(uint8_t pos, int8_t direction) {
    if (pos >= NUM_DIGITS) return;
    uint8_t phys = logical_to_physical(pos);
    uint8_t temp;
    if (direction == 1) {
        temp = display_buffer[phys][0];
        for (int row = 0; row < 6; row++) {
            display_buffer[phys][row] = display_buffer[phys][row + 1];
        }
        display_buffer[phys][6] = temp;
    } else if (direction == -1) {
        temp = display_buffer[phys][6];
        for (int row = 6; row > 0; row--) {
            display_buffer[phys][row] = display_buffer[phys][row - 1];
        }
        display_buffer[phys][0] = temp;
    }
}

void Display_RenderBuffer(void) {
    // 1. РЕНДЕРИНГ 12 ОСНОВНЫХ ЗНАКОМЕСТ (Символы)
    for (uint8_t pos = 0; pos < 12; pos++) {
        uint8_t visible = 1;

        // Обработка мигания символа
        if (char_blink[pos]) {
            visible = blink_phase;
        }

        uint64_t data_bits = 0;
        if (visible) {
            // Собираем 35 бит символа (5 колонок по 7 строк) из display_buffer
            for (int row = 0; row < 7; row++) {
                data_bits |= ((uint64_t)(display_buffer[pos][row] & 0x1F) << (row * 5));
            }
        }
        uint64_t full = data_bits << 1;

        // Раскладываем 6 байт анодов в буфер для текущей позиции
        vfd_tx_buffer[pos][0] = (full >> 40) & 0xFF;
        vfd_tx_buffer[pos][1] = (full >> 32) & 0xFF;
        vfd_tx_buffer[pos][2] = (full >> 24) & 0xFF;
        vfd_tx_buffer[pos][3] = (full >> 16) & 0xFF;
        vfd_tx_buffer[pos][4] = (full >> 8) & 0xFF;
        vfd_tx_buffer[pos][5] = full & 0xFF;

        // Рассчитываем сетку (Grid) для позиций 0..11
        if (pos < 5) {
            vfd_tx_buffer[pos][6] = 0;               // grid_low
            vfd_tx_buffer[pos][7] = 1 << (pos + 3);  // grid_high
        } else {
            vfd_tx_buffer[pos][6] = 1 << (pos - 5);  // grid_low
            vfd_tx_buffer[pos][7] = 0;               // grid_high
        }
    }

    // 2. РЕНДЕРИНГ 4 ГРУПП ПИКТОГРАММ
    for (uint8_t group = 0; group < 4; group++) {
        uint8_t pos = 12 + group; // Индексы 12, 13, 14, 15

        // Обработка мигания пиктограмм побайтно
        for (int byte = 0; byte < 6; byte++) {
            uint8_t data_byte = pict_data[group][byte];
            uint8_t blink_byte = pict_blink[group][byte];

            // Оптимизация: если в байте нет мигающих икон, копируем махом
            if (blink_byte == 0) {
                vfd_tx_buffer[pos][byte] = data_byte;
            } else {
                uint8_t result_byte = 0;
                for (int bit = 0; bit < 8; bit++) {
                    uint8_t mask = (1 << bit);
                    if (data_byte & mask) {
                        // Если бит должен мигать, и сейчас фаза "выключено" — пропускаем
                        if ((blink_byte & mask) && !blink_phase) {
                            continue;
                        }
                        result_byte |= mask;
                    }
                }
                vfd_tx_buffer[pos][byte] = result_byte;
            }
        }

        // Рассчитываем сетку (Grid) для пиктограмм (Твой оригинальный switch)
        switch (group) {
            case 0:
                vfd_tx_buffer[pos][6] = 0;
                vfd_tx_buffer[pos][7] = 1 << 0;
                break;
            case 1:
                vfd_tx_buffer[pos][6] = 0;
                vfd_tx_buffer[pos][7] = 1 << 1;
                break;
            case 2:
                vfd_tx_buffer[pos][6] = 0;
                vfd_tx_buffer[pos][7] = 1 << 2;
                break;
            case 3:
                vfd_tx_buffer[pos][6] = 1 << 7;
                vfd_tx_buffer[pos][7] = 0;
                break;
        }
    }
}
