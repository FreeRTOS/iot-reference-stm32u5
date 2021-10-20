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
#ifndef _MAIN_H
#define _MAIN_H

#include <stdint.h>
#include "stm32u5xx_hal.h"

RTC_HandleTypeDef * pxHndlRtc;
SPI_HandleTypeDef * pxHndlSpi2;
UART_HandleTypeDef * pxHndlUart1;
DCACHE_HandleTypeDef * pxHndlDCache;
DMA_HandleTypeDef * pxHndlGpdmaCh4;
DMA_HandleTypeDef * pxHndlGpdmaCh5;

typedef void ( * GPIOInterruptCallback_t ) ( void * pvContext );


void GPIO_EXTI_Register_Callback( uint16_t usGpioPinMask,
                                  GPIOInterruptCallback_t pvCallback,
                                  void * pvContext );

void hw_init( void );

int32_t ns_interface_lock_init( void );

#endif /* _MAIN_H */
