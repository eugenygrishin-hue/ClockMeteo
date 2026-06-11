#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stdint.h>
#include <stdbool.h>

#define ARCHIVE_SIZE 168

// Адреса в EEPROM
#define TEMP_AREA_START  0x00F0
#define HUM_AREA_START   (TEMP_AREA_START + ARCHIVE_SIZE * 4)
#define PRESS_AREA_START (HUM_AREA_START + ARCHIVE_SIZE * 4)
#define ARCHIVE_INDEX_ADDR (PRESS_AREA_START + ARCHIVE_SIZE * 4)

void Archive_Init(void);
bool Archive_AddEntry(double temp, double hum, double press);
bool Archive_GetLastEntry(float *temp, float *hum, float *press);
bool Archive_GetPrevEntry(float *temp, float *hum, float *press);
uint16_t Archive_GetCount(void);
float Archive_GetSpeedRaw(uint8_t type);
void Archive_DebugDump(void);
void Archive_Reset(void);
void Archive_DumpFull(void);
bool Archive_GetEntryAt(uint16_t offset, float *t, float *h, float *p);

#endif
