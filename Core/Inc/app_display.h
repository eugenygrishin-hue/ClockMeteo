#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

void APP_Display_PrepareValues(void);      // подготовка строки для дисплея (вызывается по расписанию)
void APP_Display_Update(void);             // физическое обновление дисплея (из таймера)
void APP_Display_ShowSpeed(uint8_t type);
void start_animation(uint8_t pos, uint8_t direction);
void stop_animation(void);

extern const char* months[];
extern uint8_t radio_display_mode;

#endif /* APP_DISPLAY_H */
