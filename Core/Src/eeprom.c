#include "eeprom.h"
#include "main.h"   // для hi2c2
#include <string.h> // для memcpy, memset
#include "ir_nec.h" // для IR_DebugPrint (но нужен ir_decoder)

extern IR_NEC_Decoder ir_decoder; // определён в app_ir.c

extern I2C_HandleTypeDef hi2c2;

HAL_StatusTypeDef EEPROM_Check(void) {
    return HAL_I2C_IsDeviceReady(&hi2c2, EEPROM_I2C_ADDR << 1, 3, 100);
}

HAL_StatusTypeDef EEPROM_WriteByte(uint16_t addr, uint8_t data) {
    uint8_t buffer[3];
    buffer[0] = (addr >> 8) & 0xFF;
    buffer[1] = addr & 0xFF;
    buffer[2] = data;
    return HAL_I2C_Master_Transmit(&hi2c2, EEPROM_I2C_ADDR << 1, buffer, 3, 100);
}

HAL_StatusTypeDef EEPROM_ReadByte(uint16_t addr, uint8_t *data) {
    uint8_t addr_buf[2] = {(addr >> 8) & 0xFF, addr & 0xFF};
    if (HAL_I2C_Master_Transmit(&hi2c2, EEPROM_I2C_ADDR << 1, addr_buf, 2, 100) != HAL_OK)
        return HAL_ERROR;
    return HAL_I2C_Master_Receive(&hi2c2, EEPROM_I2C_ADDR << 1, data, 1, 100);
}

// Запись блока с учётом границ страниц (страница 64 байта)
HAL_StatusTypeDef EEPROM_WriteBuffer(uint16_t start_addr, uint8_t *data, uint16_t len) {
    uint16_t addr = start_addr;
    uint16_t written = 0;
    while (written < len) {
        uint16_t page_offset = addr % EEPROM_PAGE_SIZE;
        uint16_t chunk = EEPROM_PAGE_SIZE - page_offset;
        if (chunk > (len - written)) chunk = len - written;
        uint8_t tx_buf[2 + EEPROM_PAGE_SIZE];
        tx_buf[0] = (addr >> 8) & 0xFF;
        tx_buf[1] = addr & 0xFF;
        memcpy(tx_buf + 2, data + written, chunk);
        HAL_StatusTypeDef res = HAL_I2C_Master_Transmit(&hi2c2, EEPROM_I2C_ADDR << 1, tx_buf, 2 + chunk, 100);
        if (res != HAL_OK) return res;
        HAL_Delay(5); // ожидание окончания внутреннего цикла записи (до 5 мс)
        addr += chunk;
        written += chunk;
    }
    return HAL_OK;
}

HAL_StatusTypeDef EEPROM_ReadBuffer(uint16_t start_addr, uint8_t *data, uint16_t len) {
    uint8_t addr_buf[2] = {(start_addr >> 8) & 0xFF, start_addr & 0xFF};
    if (HAL_I2C_Master_Transmit(&hi2c2, EEPROM_I2C_ADDR << 1, addr_buf, 2, 100) != HAL_OK)
        return HAL_ERROR;
    return HAL_I2C_Master_Receive(&hi2c2, EEPROM_I2C_ADDR << 1, data, len, 100);
}

void EEPROM_FullErase(void) {
    uint8_t zero_buf[64];
    memset(zero_buf, 0, sizeof(zero_buf));
    for (uint16_t addr = 0; addr < 32768; addr += 64) {
        uint16_t chunk = (32768 - addr) < 64 ? (32768 - addr) : 64;
        if (EEPROM_WriteBuffer(addr, zero_buf, chunk) != HAL_OK) {
            // Используйте ваш отладочный вывод
            IR_DebugPrint(&ir_decoder, "EEPROM write error at 0x%04X\n", addr);
            return;
        }
        HAL_Delay(5);
    }
    IR_DebugPrint(&ir_decoder, "EEPROM full erase done (0x0000-0x7FFF).\r\n");
}
