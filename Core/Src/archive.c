#include "archive.h"
#include "eeprom.h"
#include "millis.h"
#include "ir_nec.h"
#include <string.h>

extern IR_NEC_Decoder ir_decoder;

static uint16_t current_index = 0;
static uint16_t entry_count = 0;

// Сохранение индекса
static void save_index(void) {
    uint8_t buf[4];
    buf[0] = current_index & 0xFF;
    buf[1] = (current_index >> 8) & 0xFF;
    buf[2] = entry_count & 0xFF;
    buf[3] = (entry_count >> 8) & 0xFF;
    EEPROM_WriteBuffer(ARCHIVE_INDEX_ADDR, buf, 4);
}

// Загрузка индекса
static void load_index(void) {
    uint8_t buf[4];
    if (EEPROM_ReadBuffer(ARCHIVE_INDEX_ADDR, buf, 4) == HAL_OK) {
        current_index = buf[0] | (buf[1] << 8);
        entry_count = buf[2] | (buf[3] << 8);
        if (entry_count > ARCHIVE_SIZE) entry_count = ARCHIVE_SIZE;
        if (current_index >= ARCHIVE_SIZE) current_index = 0;
    } else {
        current_index = 0;
        entry_count = 0;
    }
}

static void write_float(uint32_t addr, float val) {
    union {
        float f;
        uint8_t b[4];
    } u;
    u.f = val;
    // little-endian: младший байт первым
    uint8_t buf[4] = { u.b[0], u.b[1], u.b[2], u.b[3] };
    //EEPROM_WriteBuffer(addr, buf, 4);
    if (EEPROM_WriteBuffer(addr, buf, 4) != HAL_OK) {
        IR_DebugPrint(&ir_decoder, "Write failed at 0x%04X\n", addr);
    }
}

static float read_float(uint32_t addr) {
    uint8_t buf[4];
    if (EEPROM_ReadBuffer(addr, buf, 4) != HAL_OK) return 0.0f;
    union {
        float f;
        uint8_t b[4];
    } u;
    u.b[0] = buf[0];
    u.b[1] = buf[1];
    u.b[2] = buf[2];
    u.b[3] = buf[3];
    return u.f;
}

void Archive_Init(void) {
    load_index();
}

bool Archive_AddEntry(double temp, double hum, double press) {
    // Для теста отключаем проверку часа – запись каждые 6 секунд
     static uint32_t last_hour = 0;
     uint32_t now_sec = Millis_Get() / 1000;
     uint32_t current_hour = now_sec / 3600;
     if (current_hour == last_hour) return false;
     last_hour = current_hour;

    if (entry_count < ARCHIVE_SIZE) entry_count++;
    current_index = (current_index + 1) % ARCHIVE_SIZE;

    uint32_t temp_addr = TEMP_AREA_START + current_index * 4;
    uint32_t hum_addr  = HUM_AREA_START + current_index * 4;
    uint32_t press_addr = PRESS_AREA_START + current_index * 4;

    IR_DebugPrint(&ir_decoder, "Before write: T=%.2f, H=%.1f, P=%.2f\n", temp, hum, press);

    write_float(temp_addr, temp);
    write_float(hum_addr, hum);
    write_float(press_addr, press);
    save_index();

    IR_DebugPrint(&ir_decoder, "After write: T=%.2f, H=%.1f, P=%.2f\n", temp, hum, press);

    IR_DebugPrint(&ir_decoder, "Written temp at 0x%04X: %f\n", TEMP_AREA_START + current_index*4, temp);
    IR_DebugPrint(&ir_decoder, "Written press at 0x%04X: %f\n", PRESS_AREA_START + current_index * 4, press);
    IR_DebugPrint(&ir_decoder, "Written hummid at 0x%04X: %f\n", HUM_AREA_START + current_index * 4, hum);

    IR_DebugPrint(&ir_decoder, "Archive added: idx=%d, count=%d\n", current_index, entry_count);
    return true;
}

bool Archive_GetLastEntry(float *temp, float *hum, float *press) {
    if (entry_count == 0) return false;
    uint32_t temp_addr = TEMP_AREA_START + current_index * 4;
    uint32_t hum_addr  = HUM_AREA_START + current_index * 4;
    uint32_t press_addr = PRESS_AREA_START + current_index * 4;
    *temp = read_float(temp_addr);
    *hum  = read_float(hum_addr);
    *press = read_float(press_addr);
    return true;
}

bool Archive_GetPrevEntry(float *temp, float *hum, float *press) {
    if (entry_count < 2) return false;
    uint16_t prev_idx = (current_index == 0) ? ARCHIVE_SIZE - 1 : current_index - 1;
    uint32_t temp_addr = TEMP_AREA_START + prev_idx * 4;
    uint32_t hum_addr  = HUM_AREA_START + prev_idx * 4;
    uint32_t press_addr = PRESS_AREA_START + prev_idx * 4;
    *temp = read_float(temp_addr);
    *hum  = read_float(hum_addr);
    *press = read_float(press_addr);
    return true;
}

uint16_t Archive_GetCount(void) {
    return entry_count;
}

float Archive_GetSpeedRaw(uint8_t type) {
    if (entry_count < 2) return 0.0f;
    float last_t, last_h, last_p, prev_t, prev_h, prev_p;
    if (!Archive_GetLastEntry(&last_t, &last_h, &last_p)) return 0.0f;
    if (!Archive_GetPrevEntry(&prev_t, &prev_h, &prev_p)) return 0.0f;
    switch (type) {
        case 1: return last_t - prev_t;
        case 2: return last_p - prev_p;
        case 3: return last_h - prev_h;
        default: return 0.0f;
    }
}

void Archive_DebugDump(void) {
    IR_DebugPrint(&ir_decoder, "\r\n=== Archive Dump ===\r\n");
    IR_DebugPrint(&ir_decoder, "Entry count: %d\r\n", entry_count);
    if (entry_count == 0) {
        IR_DebugPrint(&ir_decoder, "No data in archive.\r\n");
        return;
    }

    int show = (entry_count < 5) ? entry_count : 5;
    IR_DebugPrint(&ir_decoder, "Last %d entries:\r\n", show);
    for (int i = 0; i < show; i++) {
        uint16_t idx = (current_index - i + ARCHIVE_SIZE) % ARCHIVE_SIZE;
        float t = read_float(TEMP_AREA_START + idx * 4);
        float h = read_float(HUM_AREA_START + idx * 4);
        float p = read_float(PRESS_AREA_START + idx * 4);
        IR_DebugPrint(&ir_decoder, "  idx=%3d: T=%.2f, H=%.1f, P=%.2f kPa\r\n", idx, t, h, p);
    }

    if (entry_count >= 2) {
        float last_t, last_h, last_p, prev_t, prev_h, prev_p;
        if (Archive_GetLastEntry(&last_t, &last_h, &last_p) && Archive_GetPrevEntry(&prev_t, &prev_h, &prev_p)) {
            float dt = last_t - prev_t;
            float dh = last_h - prev_h;
            float dp = last_p - prev_p;
            IR_DebugPrint(&ir_decoder, "Diff (last - prev): T=%.2f, H=%.1f, P=%.2f kPa\r\n", dt, dh, dp);
            IR_DebugPrint(&ir_decoder, "Speed (per hour): T=%.2f C/h, H=%.1f %%/h, P=%.2f kPa/h\r\n", dt, dh, dp);
        }
    }
    IR_DebugPrint(&ir_decoder, "========================\r\n");
}

void Archive_Reset(void) {
    current_index = 0;
    entry_count = 0;
    save_index();
    // Очищаем первые несколько записей (опционально)
    for (int i = 0; i < 3; i++) {
        write_float(TEMP_AREA_START + i*4, 0.0f);
        write_float(HUM_AREA_START + i*4, 0.0f);
        write_float(PRESS_AREA_START + i*4, 0.0f);
    }
    IR_DebugPrint(&ir_decoder, "Archive reset done.\r\n");
}

void Archive_DumpFull(void) {
    uint8_t buf[16];
    IR_DebugPrint(&ir_decoder, "\r\n=== Full EEPROM Dump (0x0000 - 0x7FFF) ===\r\n");
    for (uint16_t addr = 0; addr < 32768; addr += 16) {
        if (EEPROM_ReadBuffer(addr, buf, 16) != HAL_OK) {
            IR_DebugPrint(&ir_decoder, "Read error at 0x%04X\r\n", addr);
            break;
        }
        IR_DebugPrint(&ir_decoder, "0x%04X: ", addr);
        for (int i = 0; i < 16; i++) {
            IR_DebugPrint(&ir_decoder, "%02X ", buf[i]);
        }
        IR_DebugPrint(&ir_decoder, " | ");
        for (int i = 0; i < 16; i++) {
            char c = (buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.';
            IR_DebugPrint(&ir_decoder, "%c", c);
        }
        IR_DebugPrint(&ir_decoder, "\r\n");
    }
    IR_DebugPrint(&ir_decoder, "========================\r\n");
}

// Позволяет получить данные за любой час в прошлом (offset: 1 = час назад, 3 = три часа назад)
bool Archive_GetEntryAt(uint16_t offset, float *t, float *h, float *p) {
    if (entry_count <= offset) return false;

    // Крутим индекс назад с учетом кольцевого буфера
    uint16_t idx = (current_index >= offset) ?
                   (current_index - offset) :
                   (ARCHIVE_SIZE + current_index - offset);

    if (t) *t = read_float(TEMP_AREA_START + idx * 4);
    if (h) *h = read_float(HUM_AREA_START + idx * 4);
    if (p) *p = read_float(PRESS_AREA_START + idx * 4);

    return true;
}


