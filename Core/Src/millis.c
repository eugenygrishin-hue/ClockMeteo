#include "millis.h"
#include "stm32f4xx_hal.h"

static volatile uint32_t ms_counter = 0;
static TIM_HandleTypeDef *timer_handle = NULL;

void Millis_Init(TIM_HandleTypeDef *htim) {
    timer_handle = htim;
    __HAL_TIM_CLEAR_IT(timer_handle, TIM_IT_UPDATE);
    HAL_TIM_Base_Start_IT(timer_handle);
}

void Millis_Inc(void) {
    ms_counter++;
}

uint32_t Millis_Get(void) {
    uint32_t ret;
    __disable_irq();
    ret = ms_counter;
    __enable_irq();
    return ret;
}
