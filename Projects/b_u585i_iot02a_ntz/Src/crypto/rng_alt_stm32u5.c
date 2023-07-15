/*
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "entropy_poll.h"

#if defined( MBEDTLS_ENTROPY_HARDWARE_ALT )

#define MBEDTLS_ENTROPY_TIMEOUT_MS    100

/*
 * include the correct headerfile depending on the STM32 family */

#include "stm32u5xx_hal.h"
#include <string.h>

extern RNG_HandleTypeDef * pxHndlRng;
static SemaphoreHandle_t xRngMutex = NULL;

static volatile TaskHandle_t xRngTaskToNotify = NULL;

static void vRngIrqHandler( void )
{
    HAL_RNG_IRQHandler( pxHndlRng );
}

static void vRngInit( void )
{
    taskENTER_CRITICAL();

    if( ( pxHndlRng != NULL ) &&
        ( xRngMutex == NULL ) )
    {
        xRngMutex = xSemaphoreCreateMutex();
        NVIC_SetVector( RNG_IRQn, ( uint32_t ) &vRngIrqHandler );
        NVIC_SetPriority( RNG_IRQn, configLIBRARY_LOWEST_INTERRUPT_PRIORITY );
        NVIC_EnableIRQ( RNG_IRQn );
    }

    taskEXIT_CRITICAL();
}

void HAL_RNG_ReadyDataCallback( RNG_HandleTypeDef * hrng,
                                uint32_t random32bit )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    ( void ) random32bit;
    ( void ) hrng;

    if( xRngTaskToNotify )
    {
        vTaskNotifyGiveFromISR( xRngTaskToNotify, &xHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

void HAL_RNG_ErrorCallback( RNG_HandleTypeDef * hrng )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    ( void ) hrng;

    if( xRngTaskToNotify )
    {
        vTaskNotifyGiveFromISR( xRngTaskToNotify, &xHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

int mbedtls_hardware_poll( void * pvCtx,
                           unsigned char * pucOutputBuffer,
                           size_t uxBufferLen,
                           size_t * puxBytesWritten )
{
    int lError = 0;
    TickType_t xTicksToWait = pdMS_TO_TICKS( MBEDTLS_ENTROPY_TIMEOUT_MS );

    TimeOut_t xTimeOut;

    ( void ) pvCtx;

    vTaskSetTimeOutState( &xTimeOut );

    if( xRngMutex == NULL )
    {
        vRngInit();
    }

    if( ( xRngMutex == NULL ) ||
        ( pucOutputBuffer == NULL ) ||
        ( uxBufferLen == 0 ) ||
        ( pxHndlRng == NULL ) ||
        ( puxBytesWritten == NULL ) )
    {
        lError = -1;
    }
    else if( xSemaphoreTake( xRngMutex, xTicksToWait ) )
    {
        size_t uxBytesWritten = 0;
        size_t uxBytesRemaining = uxBufferLen;

        xRngTaskToNotify = xTaskGetCurrentTaskHandle();

        while( uxBytesRemaining > 0 &&
               xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE )
        {
            HAL_StatusTypeDef xResult = HAL_RNG_GenerateRandomNumber_IT( pxHndlRng );

            if( xResult != HAL_OK )
            {
                lError = MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
            }
            else if( ulTaskNotifyTake( pdTRUE, xTicksToWait ) > 0 )
            {
                /* Check for error state */
                if( __HAL_RNG_GET_FLAG( pxHndlRng, RNG_FLAG_SECS | RNG_FLAG_CECS ) )
                {
                    if( __HAL_RNG_GET_FLAG( pxHndlRng, RNG_FLAG_SECS ) )
                    {
                        RNG_RecoverSeedError( pxHndlRng );
                    }
                    else
                    {
                        __HAL_RCC_RNG_CLK_DISABLE();
                        __HAL_RCC_RNG_CLK_ENABLE();
                        __HAL_RCC_RNG_FORCE_RESET();
                        __HAL_RCC_RNG_RELEASE_RESET();
                    }
                }
                else
                {
                    uint32_t ulRandomValue = HAL_RNG_ReadLastRandomNumber( pxHndlRng );

                    if( uxBytesRemaining > sizeof( uint32_t ) )
                    {
                        ( void ) memcpy( &( pucOutputBuffer[ uxBytesWritten ] ), &ulRandomValue, sizeof( uint32_t ) );

                        uxBytesWritten += sizeof( uint32_t );
                        uxBytesRemaining -= sizeof( uint32_t );
                    }
                    else if( uxBytesRemaining > 0 )
                    {
                        ( void ) memcpy( &( pucOutputBuffer[ uxBytesWritten ] ), &ulRandomValue, uxBytesRemaining );

                        uxBytesWritten = uxBufferLen;
                        uxBytesRemaining = 0;
                    }
                    else
                    {
                        uxBytesRemaining = 0;
                    }
                }
            }
            else
            {
                uxBytesRemaining = 0;
                break;
            }
        }

        xRngTaskToNotify = NULL;
        ( void ) xSemaphoreGive( xRngMutex );
        *puxBytesWritten = uxBytesWritten;
    }
    else
    {
        *puxBytesWritten = 0;
        lError = 0;
    }

    return lError;
}

#endif /*MBEDTLS_ENTROPY_HARDWARE_ALT*/
