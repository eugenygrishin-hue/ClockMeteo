#ifndef EEPROM_H
#define EEPROM_H

#include "stm32f4xx_hal.h"

#define EEPROM_I2C_ADDR 0x50   // 7-битный адрес
#define EEPROM_PAGE_SIZE 64
#define EEPROM_SIZE 32768      // 32 КБ

HAL_StatusTypeDef EEPROM_Check(void);
HAL_StatusTypeDef EEPROM_WriteByte(uint16_t addr, uint8_t data);
HAL_StatusTypeDef EEPROM_ReadByte(uint16_t addr, uint8_t *data);
HAL_StatusTypeDef EEPROM_WriteBuffer(uint16_t start_addr, uint8_t *data, uint16_t len);
HAL_StatusTypeDef EEPROM_ReadBuffer(uint16_t start_addr, uint8_t *data, uint16_t len);

void EEPROM_FullErase(void);

#endif
