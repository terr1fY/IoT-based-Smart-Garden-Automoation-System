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
#include <stdio.h>
#include "string.h"
/* Private define ------------------------------------------------------------*/
#define DS3231_ADDR     (0x68 << 1)
#define AHT10_ADDR      (0x38 << 1)

/* Calibration values */
#define MOISTURE_ADC_DRY  2000
#define MOISTURE_ADC_WET  3000
#define LIGHT_ADC_MIN     0
#define LIGHT_ADC_MAX     4095

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c2;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart1;
/* Sensor values */
uint8_t moisture_percent = 0;
uint8_t light_percent = 0;
float temperature = 0.0f;
float humidity = 0.0f;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
//static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C2_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
/* ---------------- DS3231 Functions ---------------- */
static void DS3231_ClearCH(void)
{
    uint8_t sec = 0;
    if(HAL_I2C_Mem_Read(&hi2c2, DS3231_ADDR, 0x00, 1, &sec, 1, 100) == HAL_OK){
        sec &= 0x7F;
        HAL_I2C_Mem_Write(&hi2c2, DS3231_ADDR, 0x00, 1, &sec, 1, 100);
    }
}

static void DS3231_SetTime(uint8_t h, uint8_t m, uint8_t s, uint8_t d, uint8_t mo, uint16_t y)
{
    uint8_t data[7];
    data[0] = ((s/10)<<4) | (s%10);
    data[1] = ((m/10)<<4) | (m%10);
    data[2] = ((h/10)<<4) | (h%10);
    data[3] = 1; // day of week
    data[4] = ((d/10)<<4) | (d%10);
    data[5] = ((mo/10)<<4) | (mo%10);
    data[6] = (((y-2000)/10)<<4) | ((y-2000)%10);

    HAL_I2C_Mem_Write(&hi2c2, DS3231_ADDR, 0x00, 1, data, 7, 200);
}

static void DS3231_ReadTime(uint8_t *h, uint8_t *m, uint8_t *s,
                            uint8_t *d, uint8_t *mo, uint16_t *y)
{
    uint8_t data[7];
    if(HAL_I2C_Mem_Read(&hi2c2, DS3231_ADDR, 0x00, 1, data, 7, 200) != HAL_OK){
        *h = *m = *s = *d = *mo = 0;
        *y = 2000;
        return;
    }
    uint8_t sec = data[0] & 0x7F;
    *s  = ((sec>>4)*10) + (sec&0x0F);
    *m  = ((data[1]>>4)*10) + (data[1]&0x0F);
    *h  = ((data[2]>>4)*10) + (data[2]&0x0F);
    *d  = ((data[4]>>4)*10) + (data[4]&0x0F);
    *mo = ((data[5]>>4)*10) + (data[5]&0x0F);
    *y  = 2000 + ((data[6]>>4)*10) + (data[6]&0x0F);
}

/* ---------------- ADC Helpers ---------------- */
uint32_t read_adc_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    uint32_t value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return value;
}

uint8_t read_moisture_percent(uint32_t channel)
{
    uint32_t adc = read_adc_channel(channel);
    if(adc <= MOISTURE_ADC_DRY) return 0;
    if(adc >= MOISTURE_ADC_WET) return 100;
    return (uint8_t)((adc - MOISTURE_ADC_DRY)*100 / (MOISTURE_ADC_WET - MOISTURE_ADC_DRY));
}

uint8_t read_light_percent(uint32_t channel)
{
    uint32_t adc = read_adc_channel(channel);
    if(adc < LIGHT_ADC_MIN) adc = LIGHT_ADC_MIN;
    if(adc > LIGHT_ADC_MAX) adc = LIGHT_ADC_MAX;
    return (uint8_t)((adc - LIGHT_ADC_MIN)*100 / (LIGHT_ADC_MAX - LIGHT_ADC_MIN));
}

/* ---------------- AHT10 Functions ---------------- */
void read_aht10(void)
{
    uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    uint8_t buf[6];
    if(HAL_I2C_Master_Transmit(&hi2c2, AHT10_ADDR, cmd, 3, 100) != HAL_OK) return;
    HAL_Delay(80);
    if(HAL_I2C_Master_Receive(&hi2c2, AHT10_ADDR, buf, 6, 100) != HAL_OK) return;
    if(buf[0] & 0x80) return;

    uint32_t raw_h = ((uint32_t)buf[1]<<12)|((uint32_t)buf[2]<<4)|((buf[3]>>4)&0x0F);
    uint32_t raw_t = ((uint32_t)(buf[3]&0x0F)<<16)|((uint32_t)buf[4]<<8)|buf[5];
    humidity = (float)raw_h * 100.0f / 1048576.0f;
    temperature = (float)raw_t * 200.0f / 1048576.0f - 50.0f;
}

/* ---------------- Main ---------------- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
//    MX_DMA_Init();
    MX_ADC1_Init();
    MX_I2C2_Init();
    MX_USART2_UART_Init();
    MX_USART1_UART_Init();
    // Thiết lập thời gian DS3231 (nếu cần)
    HAL_Delay(200);
    DS3231_ClearCH();

    while(1)
    {
        // 1. Đọc DS3231
        uint8_t hh, mm, ss, dd, mon;
        uint16_t yyyy;
        DS3231_ReadTime(&hh, &mm, &ss, &dd, &mon, &yyyy);

        // 2. Đọc ADC
        moisture_percent = 100 - read_moisture_percent(ADC_CHANNEL_1);
        light_percent = 100 - read_light_percent(ADC_CHANNEL_4);

        // 3. Đọc AHT10
        read_aht10();

        // 4. In ra UART
        char buf[128];
        int len = snprintf(buf, sizeof(buf),
                           "{\"temp\":%.2f,\"humid\":%.2f,\"light\":%u,\"soil\":%u}\n",
                           temperature,
                           humidity,
                           light_percent,
                           moisture_percent);
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, len, 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)buf, len, 100);
        HAL_Delay(1000);
    }
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
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
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

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
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}
static void MX_USART1_UART_Init(void)
{
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
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);


  /* USER CODE BEGIN MX_GPIO_Init_2 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
#ifdef USE_FULL_ASSERT
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
