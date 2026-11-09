/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "can.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 节点 ID：A 板 = 0x201，B 板 = 0x202。
 * 两块板烧同一份代码，只改这一个数字。 */
#define NODE_ID   0x201u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_CAN_Init();
  /* USER CODE BEGIN 2 */
/* ① 过滤器：必须在 Start 之前配置。
 *    HAL_CAN_ConfigFilter 会先进入 Init 模式再退出，
 *    已经 Start 过再调会返回错误、卡死在 Error_Handler。
 *    掩码全 0 = 不筛，全部接收。 */
CAN_FilterTypeDef canFilter;
canFilter.FilterBank           = 0;
canFilter.FilterMode           = CAN_FILTERMODE_IDMASK;
canFilter.FilterScale          = CAN_FILTERSCALE_32BIT;
canFilter.FilterIdHigh         = 0x0000;
canFilter.FilterIdLow          = 0x0000;
canFilter.FilterMaskIdHigh     = 0x0000;
canFilter.FilterMaskIdLow      = 0x0000;
canFilter.FilterFIFOAssignment = CAN_RX_FIFO0;
canFilter.FilterActivation     = ENABLE;
canFilter.SlaveStartFilterBank = 14;
if (HAL_CAN_ConfigFilter(&hcan, &canFilter) != HAL_OK)
{
    Error_Handler();
}

/* ② 启动 CAN */
if (HAL_CAN_Start(&hcan) != HAL_OK)
{
    Error_Handler();
}

/* ③ 板载 LED：PC13，低电平点亮 */
__HAL_RCC_GPIOC_CLK_ENABLE();
GPIO_InitTypeDef ledGpio = {0};
ledGpio.Pin   = GPIO_PIN_13;
ledGpio.Mode  = GPIO_MODE_OUTPUT_PP;
ledGpio.Pull  = GPIO_NOPULL;
ledGpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &ledGpio);

CAN_TxHeaderTypeDef txHeader;
CAN_RxHeaderTypeDef rxHeader;
uint32_t txMailbox = 0;
uint8_t  txData[8] = {0};
uint8_t  rxData[8] = {0};
uint8_t  rxCount = 0;   /* 收到「别人」的帧数，放进 txData[2] 作为回显 */
uint16_t cnt = 0;

txHeader.StdId = NODE_ID;
txHeader.ExtId = 0;
txHeader.IDE   = CAN_ID_STD;
txHeader.RTR   = CAN_RTR_DATA;
txHeader.DLC   = 8;
txHeader.TransmitGlobalTime = DISABLE;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 接收：收到「别人」的帧就计数（这块板没有 LED，用回显验证） */
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK)
    {
      if (rxHeader.StdId != NODE_ID)
      {
        rxCount++;
      }
    }

    txData[0] = (uint8_t)(cnt & 0xFF);
    txData[1] = (uint8_t)((cnt >> 8) & 0xFF);
    txData[2] = rxCount;   /* 第 3 字节 = 我收到了多少帧 */

    if (HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &txMailbox) == HAL_OK)
    {
      cnt++;
    }

    HAL_Delay(20);   /* 20 ms = 50 帧/秒 */
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
