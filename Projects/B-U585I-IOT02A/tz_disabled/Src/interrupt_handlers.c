/*
 * FreeRTOS STM32 Reference Integration
 *
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
#include "FreeRTOS.h"
#include "task.h"

extern DMA_HandleTypeDef handle_GPDMA1_Channel7;
extern DMA_HandleTypeDef handle_GPDMA1_Channel6;
extern DMA_HandleTypeDef handle_GPDMA1_Channel5;
extern DMA_HandleTypeDef handle_GPDMA1_Channel4;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim6;


void NMI_Handler(void)
{
  while (1)
  {
  }
}

void prvGetRegistersFromStack( uint32_t *pulFaultStackAddress )
{
    /* These are volatile to try and prevent the compiler/linker optimising them
    away as the variables never actually get used.  If the debugger won't show the
    values of the variables, make them global my moving their declaration outside
    of this function. */
    volatile uint32_t r0;
    volatile uint32_t r1;
    volatile uint32_t r2;
    volatile uint32_t r3;
    volatile uint32_t r12;
    volatile uint32_t lr;   /* Link register. */
    volatile uint32_t pc;   /* Program counter. */
    volatile uint32_t psr;  /* Program status register. */

    r0 = pulFaultStackAddress[ 0 ];
    r1 = pulFaultStackAddress[ 1 ];
    r2 = pulFaultStackAddress[ 2 ];
    r3 = pulFaultStackAddress[ 3 ];

    r12 = pulFaultStackAddress[ 4 ];
    lr = pulFaultStackAddress[ 5 ];
    pc = pulFaultStackAddress[ 6 ];
    psr = pulFaultStackAddress[ 7 ];

    /* When the following line is hit, the variables contain the register values. */
    for( ;; );
}

void HardFault_Handler( void ) __attribute__( ( naked ) );

void HardFault_Handler(void)
{
    __asm volatile
    (
        " tst lr, #4                                                \n"
        " ite eq                                                    \n"
        " mrseq r0, msp                                             \n"
        " mrsne r0, psp                                             \n"
        " ldr r1, [r0, #24]                                         \n"
        " ldr r2, handler2_address_const                            \n"
        " bx r2                                                     \n"
        " handler2_address_const: .word prvGetRegistersFromStack    \n"
    );
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

extern void SysTick_Handler( void );

void _SysTick_Handler( void )
{
    /* Clear overflow flag */
    SysTick->CTRL;

    if ( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
    {
    /* Call the cortex-m33 port systick handler */
        SysTick_Handler();
    }
}
