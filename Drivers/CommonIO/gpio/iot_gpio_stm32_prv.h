/*
 * FreeRTOS Common IO V0.1.1
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
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */
#ifndef _IOT_GPIO_STM32_PRV_
#define _IOT_GPIO_STM32_PRV_

#include "iot_gpio.h"
#include "stm32u585xx.h"
#include "stm32u5xx_hal.h"

/* Number of GPIO pins per controller. Also defines number of interrupts */
#define NUM_GPIO_PER_CONTROLLER 16


/* Private header file for STM32 HAL gpio driver */
typedef struct
{
    IotGpioDirection_t xDirection;
    IotGpioOutputMode_t xOutMode;
    int32_t lSpeed;
    IotGpioPull_t xPull;
    IotGpioInterrupt_t xInterruptMode;
    int32_t lFunction;
} IotGpioConfig_t;

typedef struct
{
    GPIO_TypeDef * xPort;
    uint16_t xPinMask;
    IRQn_Type xIRQ;
} IotMappedPin_t;

typedef enum
{
    IOT_GPIO_CLOSED = 0u,
    IOT_GPIO_OPENED = 1u
} IotGpioState_t;


/**
 * @brief   GPIO descriptor type
 */
typedef struct IotGpioDescriptor
{
    int32_t lGpioNumber;
    IotGpioConfig_t xConfig;
    IotGpioCallback_t xUserCallback;
    void * pvUserContext;
    uint8_t ucState;
} IotGpioDescriptor_t;

#endif /* _IOT_GPIO_STM32_PRV_ */
