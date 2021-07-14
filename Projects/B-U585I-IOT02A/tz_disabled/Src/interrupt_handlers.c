/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#include "main.h"
#include "stm32u5xx_it.h"

extern DMA_HandleTypeDef handle_GPDMA1_Channel7;
extern DMA_HandleTypeDef handle_GPDMA1_Channel6;
extern DMA_HandleTypeDef handle_GPDMA1_Channel5;
extern DMA_HandleTypeDef handle_GPDMA1_Channel4;
extern SPI_HandleTypeDef hspi2;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim6;


void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

/* Note: SVC_Handler and PendSV_Handler are provided by the FreeRTOS Cortex-M33 port */

void DebugMon_Handler(void)
{
}

/* STM32U5xx Peripheral Interrupt Handlers */

void EXTI14_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);
}

void EXTI15_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
}

void GPDMA1_Channel4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel4);
}

void GPDMA1_Channel5_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel5);
}

void GPDMA1_Channel6_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel6);
}

void GPDMA1_Channel7_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel7);
}

/* Handle TIM6 interrupt for STM32 HAL time base. */
void TIM6_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim6);
}

void SPI2_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&hspi2);
}

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart1);
}
