/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32f4xx_hal.h"
#include "vfd_driver.h"
#include "millis.h"
#include "task_scheduler.h"
#include "sensor_config.h"
#include "sensor_manager.h"
#include "app_states.h"
#include "app_display.h"
#include "app_ir_handlers.h"
#include "app_ir.h"
#include "ir_config.h"
#include "eeprom.h"
#include "ds3232.h"
#include "tef6686.h"
#include <stdio.h>


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */


/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
I2C_HandleTypeDef hi2c3;

IWDG_HandleTypeDef hiwdg;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim9;
TIM_HandleTypeDef htim11;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
extern uint32_t radio_msg_end;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM11_Init(void);
static void MX_I2C2_Init(void);
static void MX_I2C3_Init(void);
static void MX_TIM9_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_IWDG_Init(void);
/* USER CODE BEGIN PFP */


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void Task_SensorManager(void) { SensorManager_Process(); }
static void Task_DisplayPrepare(void) { APP_Display_PrepareValues(); }

static void Task_StateMachine(void) {
	Radio_Seek_Service();
	StateMachine_Process();
}

static void Task_IR_Process(void) {
    APP_IR_Process(); // Декодирует сигналы и помещает события в очередь
    IR_ProcessEvents(); // Обрабатывает события из очереди
}

static void Task_Render(void) {
	Display_RenderBuffer();
    if (StateMachine_GetState() == STATE_RADIO) {
        update_radio_indicators();
    }
}

// Задача для обновления индикаторов радио (вызывается каждые 1000 мс)
static void RadioIndicatorsTask(void) {
}


static void RDS_Task(void) {
    if (StateMachine_GetState() == STATE_RADIO) {
        Radio_ProcessRDS();
    }
}



// Функция сканирования I2C шины
static void I2C_Scan(I2C_HandleTypeDef *hi2c, const char *bus_name) {
    char msg[32];
    snprintf(msg, sizeof(msg), "Scan %s...", bus_name);
    Display_PrintString(0, msg);
#ifdef IR_DEBUG
    IR_DebugPrint(&ir_decoder, "%s\r\n", msg);
#endif

    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        if (HAL_I2C_IsDeviceReady(hi2c, addr << 1, 3, 50) == HAL_OK) {
            found = 1;
            snprintf(msg, sizeof(msg), "Found: 0x%02X", addr);
            Display_PrintString(0, msg);
#ifdef IR_DEBUG
            IR_DebugPrint(&ir_decoder, "Device at 0x%02X\r\n", addr);
#endif
            HAL_Delay(800); // пауза, чтобы увидеть на дисплее
        }
    }
    if (!found) {
        Display_PrintString(0, "No devices");
#ifdef IR_DEBUG
        IR_DebugPrint(&ir_decoder, "No devices found\r\n");
#endif
    }
    HAL_Delay(1500);
}

static void EEPROM_FullTest(void) {
    char msg[32];
    Display_PrintString(0, "EEPROM test...");
    HAL_Delay(1000);

    uint16_t errors = 0;
    uint8_t write_byte, read_byte;

    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr++) {
        write_byte = (uint8_t)(addr & 0xFF);
        if (EEPROM_WriteByte(addr, write_byte) != HAL_OK) {
            errors++;
            continue;
        }
        HAL_Delay(5); // задержка на внутреннюю запись (макс 5 мс)

        if (EEPROM_ReadByte(addr, &read_byte) != HAL_OK) {
            errors++;
            continue;
        }
        if (read_byte != write_byte) {
            errors++;
        }

        // Индикация прогресса каждые 1024 байта
        if ((addr + 1) % 1024 == 0) {
            snprintf(msg, sizeof(msg), "Addr: %d", addr + 1);
            Display_PrintString(0, msg);
        }
    }

    if (errors == 0) {
        snprintf(msg, sizeof(msg), "OK, %d", EEPROM_SIZE);
    } else {
        snprintf(msg, sizeof(msg), "Errors: %d", errors);
    }
    Display_PrintString(0, msg);
    HAL_Delay(5000);
}

/*void Task_ArchiveData(void) {
    static uint32_t last_archive_check = 0;
    uint32_t now = Millis_Get() / 1000;
    uint32_t current_hour = now / 3600;
    if (current_hour != last_archive_check) {
        last_archive_check = current_hour;
        Sensor_t *bme = SensorManager_GetSensor("BME280");
        if (bme && bme->status == SENSOR_OK) {
            float t = bme->get_value(bme->instance, 0);
            float h = bme->get_value(bme->instance, 1);
            float p = bme->get_value(bme->instance, 2);
            Archive_AddEntry(t, h, p);
        }
    }
}*/

void Task_ArchiveData(void) {
    Sensor_t *bme = SensorManager_GetSensor("BME280");
    if (bme && bme->status == SENSOR_OK) {
        float t = bme->get_value(bme->instance, 0);
        float h = bme->get_value(bme->instance, 1);
        float p = bme->get_value(bme->instance, 2);
        //IR_DebugPrint(&ir_decoder, "Archive call: T=%.2f, H=%.1f, P=%.2f\n", t, h, p);
        Archive_AddEntry(t, h, p);   //
        //Archive_AddEntry(22.72f, 42.5f, 988.39f);
    } else {
        IR_DebugPrint(&ir_decoder, "BME280 not ready\n");
    }
}

void I2C_UnbusyReset(void) {
    GPIO_InitTypeDef gpio = {0};
    // Перенастроить пины как обычный GPIO Output Open-Drain
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7; // ваши пины
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    // Генерируем 9 тактов на SCL, пока SDA отпущена
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);   // SCL = 1
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET); // SCL = 0
        HAL_Delay(1);
    }
    // Формируем STOP условие: SDA = 0, SCL = 1, SDA = 1
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    HAL_Delay(1);

    // Возвращаем пины в режим Alternate Function
    MX_I2C1_Init(); // ваша функция инициализации I2C
}

void I2C_ForceReset(void) {
    // 1. Полностью отключаем I2C-периферию
    HAL_I2C_DeInit(&hi2c1);

    // 2. Принудительно переводим пины SCL и SDA в режим входа (Input)
    //    Это "отпустит" линии, сняв с них низкий уровень от MCU.
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = GPIO_PIN_6 | GPIO_PIN_7; // Укажите ваши пины SCL/SDA
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio_init); //

    HAL_Delay(100);

    // 3. Повторно инициализируем I2C с корректными настройками
    MX_I2C1_Init(); // Или HAL_I2C_Init(&hi2c1);
}

// Передаем просто числа, чтобы не зависеть от инклудов и структур
void Alarm_Check_Task(void) {
    // 1. Проверка Snooze (используем внешние переменные)
    extern bool is_snooze_active;
    extern uint32_t snooze_time_ms;
    DS3232_Time rtc_time; // Создаем структуру

    if (is_snooze_active) {
        if (Millis_Get() >= snooze_time_ms) {
            is_snooze_active = false;
            StateMachine_SetState(STATE_ALARM_RINGING);
            return;
        }
    }

    if (DS3232_GetTime(&rtc_time) == HAL_OK) {
        // Теперь передаем в будильник ГАРАНТИРОВАННО верные данные
        // h, m, weekday
    	uint32_t h = rtc_time.hour;
    	uint32_t m = rtc_time.minute;
    	uint32_t wd = rtc_time.weekday;

		// 2. Проверка будильников
		static uint8_t last_min = 61;
		if (m == last_min) return;
		last_min = m;

		// Сдвигаем логику проверки в app_states.c или подключаем структуру
		// Но проще вызвать внешнюю функцию, которая "знает" про alarm_db
		IR_DebugPrint(&ir_decoder, "Check alarm - %02d:%02d %02d\n", h, m, wd);
		Alarm_Check_Logic(h, m, wd);
    }
}



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();
  MX_TIM11_Init();
  MX_I2C2_Init();
  MX_I2C3_Init();
  MX_TIM9_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */


    HAL_GPIO_WritePin(GPIOC, LED_Pin, GPIO_PIN_RESET);
    Display_Init();

    Millis_Init(&htim11);

    // Инициализация датчиков
    SENSOR_InitAll();

    // Инициализация очереди
    //IR_Event_Queue_Init();

    // Инициализация планировщика
    TaskScheduler_Init();
    TaskScheduler_AddTask(Task_SensorManager, 10, "Sensors");
    TaskScheduler_AddTask(Task_DisplayPrepare, 250, "Display");
    TaskScheduler_AddTask(Task_StateMachine, 10, "StateMachine");
    TaskScheduler_AddTask(Task_IR_Process, 10, "IR");
    TaskScheduler_AddTask(RDS_Task, 200, "RDS");               // добавлена задача RDS
    //TaskScheduler_AddTask(RadioIndicatorsTask, 1000, "RadioInd");
    TaskScheduler_AddTask(Task_ArchiveData, 60000, "Archive");
    TaskScheduler_AddTask(Alarm_Check_Task, 6000, "Alarm");
    TaskScheduler_AddTask(Task_Render, 50, "Render");

    // Инициализация автомата состояний
    APP_States_Init();

    // Инициализация ИК-приёмника
    APP_IR_Init();

    // Инициализация архива
    Archive_Init();

    // Инициализация будильника
    Alarm_Init();

    // Устанавливаем стартовое состояние (Часы) и сразу зажигаем нужные иконки
    StateMachine_SetState(STATE_MAIN);

    //Archive_Reset();

    //EEPROM_FullErase();
    //EEPROM_FullTest();

    //Archive_DebugDump();
    //Archive_DebugDump();
    //HAL_Delay(3000);
    //Archive_DumpFull();

    //I2C_ForceReset();

    //HAL_Delay(3000);

    // Сканирование шины hi2c1 (TEF6686)
    //I2C_Scan(&hi2c1, "I2C1");
    // Сканирование шины hi2c2 (DS3231, 24C256)
    //I2C_Scan(&hi2c2, "I2C2");
    // Сканирование шины hi2c3 (VEML7700)
    //I2C_Scan(&hi2c3, "I2C3");

    //if (HAL_I2C_IsDeviceReady(&hi2c1, 0x11 << 1, 3, 100) == HAL_OK) {
    //    IR_DebugPrint(&ir_decoder, "Radio module found at 0x11\n");
        //I2C_UnbusyReset();
    //} else {
    //    IR_DebugPrint(&ir_decoder, "Radio module NOT found at 0x11\n");
    //}

    //Sensor_t *bme = SensorManager_GetSensor("BME280");
    //if (bme) IR_DebugPrint(&ir_decoder, "BME280 status: %d\n", bme->status);

    // Принудительно зажжём пиктограммы
    /*for (int g = 0; g < 4; g++) {
        for (int p = 0; p < 20; p++) {
            set_pictogram(g, p);
        }
    }
    HAL_Delay(5000);
    // Погасим
    for (int g = 0; g < 4; g++) {
        for (int p = 0; p < 20; p++) {
            clear_pictogram(g, p);
        }
    }*/

    HAL_IWDG_Start(&hiwdg);

    while(1) { HAL_Delay(100); }

    IR_DebugPrint(&ir_decoder, "✅ IWDG Started (Timeout: ~3 sec)\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	    HAL_IWDG_Refresh(&hiwdg);

	    uint16_t a; uint8_t c;
	    if (APP_IR_GetCommand(&a, &c)) {
	        if (a == 0x010E && c == 0x01) {
	            SystemState_t current = StateMachine_GetState();
	            if (current == STATE_RADIO) {
	                radio_show_message("Radio Off", 2000);
	                radio_msg_end = Millis_Get() + 2000;
	            } else if (current == STATE_EDIT_ALARM || current == STATE_ALARM_SELECT_RADIO) {
	                Alarm_Init();
	            }

	            if (current == STATE_RADIO || current == STATE_EDIT_ALARM || current == STATE_ALARM_SELECT_RADIO) {
	                Radio_Mute(true);
	                Radio_PowerOff();
	                TaskScheduler_SetTaskEnabled("RDS", false);
	                enable_sensor("BME280", true, 0);
	                enable_sensor("DS3231", true, 0);
	            }

	            is_snooze_active = false;
	            StateMachine_SetState(STATE_MAIN);
	            continue;
	        }
	        APP_IR_PushBack(a, c);
	    }

	    StateMachine_Process();
	    TaskScheduler_Run();
	    __WFI();               // переход в режим пониженного энергопотребления
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 84;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000;
  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.ClockSpeed = 50000;
  hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

}

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg.Init.Reload = 1500;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 83;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 299;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  HAL_TIM_Base_Start_IT(&htim2);  // Таймер начнёт генерировать прерывания каждые 1 мс

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM9 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM9_Init(void)
{

  /* USER CODE BEGIN TIM9_Init 0 */

  /* USER CODE END TIM9_Init 0 */

  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM9_Init 1 */

  /* USER CODE END TIM9_Init 1 */
  htim9.Instance = TIM9;
  htim9.Init.Prescaler = 84-1;
  htim9.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim9.Init.Period = 65535;
  htim9.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim9.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_IC_Init(&htim9) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim9, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM9_Init 2 */

  // Включаем прерывания для обоих каналов
  __HAL_TIM_ENABLE_IT(&htim9, TIM_IT_CC1);
  HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);

  // Запускаем таймер и оба канала захвата
  HAL_TIM_Base_Start(&htim9);
  HAL_TIM_IC_Start_IT(&htim9, TIM_CHANNEL_1);

  /* USER CODE END TIM9_Init 2 */

}

/**
  * @brief TIM11 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM11_Init(void)
{

  /* USER CODE BEGIN TIM11_Init 0 */

  /* USER CODE END TIM11_Init 0 */

  /* USER CODE BEGIN TIM11_Init 1 */

  /* USER CODE END TIM11_Init 1 */
  htim11.Instance = TIM11;
  htim11.Init.Prescaler = 84-1;
  htim11.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim11.Init.Period = 1000-1;
  htim11.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim11.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim11) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM11_Init 2 */

  /* USER CODE END TIM11_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LE_Pin|STR_Pin|TUNER_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LE_Pin STR_Pin TUNER_RST_Pin */
  GPIO_InitStruct.Pin = LE_Pin|STR_Pin|TUNER_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM2) {
        APP_Display_Update();   // обновление дисплея
    }
    if (htim->Instance == TIM11) {
        Millis_Inc();           // счётчик миллисекунд
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM9) {
        bool is_rising_edge = (TIM9->CCER & TIM_CCER_CC1P) == 0;
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
            is_rising_edge ? TIM_ICPOLARITY_FALLING : TIM_ICPOLARITY_RISING);
        uint32_t timestamp = htim->Instance->CCR1;
        IR_NEC_ProcessEdge(&ir_decoder, is_rising_edge, timestamp);
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
