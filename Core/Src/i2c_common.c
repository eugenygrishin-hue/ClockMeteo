#include "i2c_common.h"
#include "main.h"
#include "ir_nec.h"

// Убедитесь, что здесь указан правильный I2C handle, используемый для TEF6686.
// Если радиомодуль подключён к hi2c1, замените hi2c3 на hi2c1.
extern I2C_HandleTypeDef hi2c1;   // I2C для радиомодуля
extern IR_NEC_Decoder ir_decoder;

bool I2C_Write16(uint8_t dev_addr, uint8_t reg_addr, uint16_t data) {
    uint8_t buf[3] = {reg_addr, (data >> 8) & 0xFF, data & 0xFF};
    HAL_StatusTypeDef res = HAL_I2C_Master_Transmit(&hi2c1, dev_addr << 1, buf, 3, 100);
    if (res != HAL_OK) {
        IR_DebugPrint(&ir_decoder, "I2C_W: dev=0x%02X reg=0x%02X data=0x%04X => FAIL (code %d)\r\n",
                      dev_addr, reg_addr, data, res);
        return false;
    }
    // При необходимости раскомментировать для отладки:
    // IR_DebugPrint(&ir_decoder, "I2C_W: dev=0x%02X reg=0x%02X data=0x%04X => OK\r\n",
    //               dev_addr, reg_addr, data);
    return true;
}

bool I2C_Read16(uint8_t dev_addr, uint8_t reg_addr, uint16_t *data) {
    if (data == NULL) {
        IR_DebugPrint(&ir_decoder, "I2C_R: NULL pointer\r\n");
        *data = 0;

        return false;
    }
    uint8_t buf[2];
    HAL_StatusTypeDef res = HAL_I2C_Mem_Read(&hi2c1, dev_addr << 1, reg_addr, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
    if (res != HAL_OK) {
        IR_DebugPrint(&ir_decoder, "I2C_R: dev=0x%02X reg=0x%02X => FAIL (code %d)\r\n", dev_addr, reg_addr, res);
        return false;
    }
    *data = (buf[0] << 8) | buf[1];
    // IR_DebugPrint(&ir_decoder, "I2C_R: dev=0x%02X reg=0x%02X => 0x%04X\r\n", dev_addr, reg_addr, *data);
    return true;
}
