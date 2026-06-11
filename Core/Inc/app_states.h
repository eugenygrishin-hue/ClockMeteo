#ifndef APP_STATES_H
#define APP_STATES_H

#include "state_machine.h"   // для StateDescriptor_t

extern bool rds_active;
extern uint8_t current_ringing_alarm_idx;

extern uint32_t snooze_time_ms;
extern bool is_snooze_active;

extern const StateDescriptor_t app_state_table[STATE_COUNT];
void APP_States_Init(void);   // инициализация автомата
void update_radio_indicators(void);
void radio_show_message(const char *msg, uint32_t duration_ms);
void Alarm_Check_Logic(uint8_t h, uint8_t m, uint8_t wd);
bool get_is_snooze_active(void);
void enable_sensor(const char* name, bool enable, uint32_t interval_ms);

#endif /* APP_STATES_H */
