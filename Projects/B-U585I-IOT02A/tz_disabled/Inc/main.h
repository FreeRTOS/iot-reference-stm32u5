/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u5xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define MXCHIP_FLOW_Pin GPIO_PIN_15
#define MXCHIP_FLOW_GPIO_Port GPIOG
#define MXCHIP_FLOW_EXTI_IRQn EXTI15_IRQn
#define LED_RED_Pin GPIO_PIN_6
#define LED_RED_GPIO_Port GPIOH
#define LED_GREEN_Pin GPIO_PIN_7
#define LED_GREEN_GPIO_Port GPIOH
#define MXCHIP_NOTIFY_Pin GPIO_PIN_14
#define MXCHIP_NOTIFY_GPIO_Port GPIOD
#define MXCHIP_NOTIFY_EXTI_IRQn EXTI14_IRQn
#define MXCHIP_NSS_Pin GPIO_PIN_12
#define MXCHIP_NSS_GPIO_Port GPIOB
#define MXCHIP_RESET_Pin GPIO_PIN_15
#define MXCHIP_RESET_GPIO_Port GPIOF
void   MX_ICACHE_Init(void);
void   MX_GPIO_Init(void);
void   MX_USART1_UART_Init(void);
void   MX_RNG_Init(void);
void   MX_RTC_Init(void);
void   MX_GPDMA1_Init(void);
void   MX_SPI2_Init(void);
void   MX_I2C2_Init(void);
void   MX_PKA_Init(void);
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
