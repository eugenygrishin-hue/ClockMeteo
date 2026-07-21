#include "app_ir_handlers.h"
#include "app_ir.h"
#include "state_machine.h"

bool IR_ProcessEvents(void) {
    uint16_t addr;
    uint8_t cmd;

    if (APP_IR_GetCommand(&addr, &cmd)) {
        // Здесь обрабатываем ТОЛЬКО команды, которые должны работать ВЕЗДЕ (например, выключение)
        // (addr == 0x010E && cmd == 0x01) {
            // Кнопка Power - глобальное выключение
            // (Логика выключения уже есть в main.c, но если нужно здесь - оставь)
            //return true;
        //}

        // ✅ КРИТИЧНО: ВСЕ остальные команды (включая 0x0A, 0x41, 0xC1 и т.д.)
        // мы ВОЗВРАЩАЕМ в очередь, чтобы их мог прочитать текущий обработчик состояния!
        APP_IR_PushBack(addr, cmd);
    }

    return false;
}
