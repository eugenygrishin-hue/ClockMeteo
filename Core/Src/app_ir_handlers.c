#include "app_ir_handlers.h"
#include "app_ir.h"
#include "state_machine.h"
#include "tef6686.h" // Если нужно управлять громкостью

bool IR_ProcessEvents(void) {
    uint16_t addr;
    uint8_t cmd;

    if (APP_IR_GetCommand(&addr, &cmd)) {
        // Здесь обрабатываем ТОЛЬКО глобальные команды, которые должны работать ВЕЗДЕ
        if (addr == 0x414E) {
            if (cmd == 0x0A) { // Пример: кнопка громкости (замени на свой код)
                // Radio_Volume_Up();
                return true; // Команда обработана, дальше не передаем
            }
        }

        // ✅ КРИТИЧНО: Если это НЕ глобальная команда, возвращаем её в очередь,
        // чтобы её мог прочитать текущий обработчик состояния (STATE_EDIT_ALARM, STATE_RADIO и т.д.)
        APP_IR_PushBack(addr, cmd);
    }

    return false;
}
