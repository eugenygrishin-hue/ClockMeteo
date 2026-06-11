#ifndef I2C_COMMON_H
#define I2C_COMMON_H

#include <stdint.h>
#include <stdbool.h>

// Запись 16-битного значения в регистр устройства I2C.
// Возвращает true при успешной передаче.
bool I2C_Write16(uint8_t dev_addr, uint8_t reg_addr, uint16_t data);

// Чтение 16-битного значения из регистра устройства I2C.
// Возвращает true при успешном чтении, иначе false.
bool I2C_Read16(uint8_t dev_addr, uint8_t reg_addr, uint16_t *data);

#endif
