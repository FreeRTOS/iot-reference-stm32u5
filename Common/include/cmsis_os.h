/*
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

#ifndef _CMSIS_OS_COMPAT
#define _CMSIS_OS_COMPAT
#include "FreeRTOS.h"
#include "semphr.h"


/* Partial implementation of the cmsis-rtos semaphore API for use with STM32 BSP functions */

#define osSemaphoreDef( name )

#define osSemaphore( name )    ( xSemaphore ## name )

#define osSemaphoreId    SemaphoreHandle_t

#define osSemaphoreCreate( name, count ) \
    ( count == 1 ? xSemaphoreCreateMutex() : xSemaphoreCreateCounting( count, count ) )

typedef enum
{
    osOK = 0,                /*/< function completed; no error or event occurred. */
    osEventTimeout = 0x40,   /*/< function completed; timeout occurred. */
    osErrorParameter = 0x80, /*/< parameter error: a mandatory parameter was missing or specified an incorrect object. */
    osErrorResource = 0x81,  /*/< resource not available: a specified resource was not available. */
    osErrorTimeoutResource = 0xC1
} osStatus;

#define osWaitForever    0xFFFFFFFF      /*/< wait forever timeout value */

static inline int32_t osSemaphoreWait( SemaphoreHandle_t xSemaphore,
                                       uint32_t ulWaitMs )
{
    int32_t lRetVal = 0;
    TickType_t xWaitTime;

    if( xSemaphore == NULL )
    {
        lRetVal = -1;
    }
    else if( ulWaitMs == osWaitForever )
    {
        xWaitTime = portMAX_DELAY;
    }
    else
    {
        xWaitTime = pdMS_TO_TICKS( ulWaitMs );
    }

    if( ( lRetVal == 0 ) &&
        ( xSemaphoreTake( xSemaphore, xWaitTime ) == pdTRUE ) )
    {
        lRetVal = ( int32_t ) uxSemaphoreGetCount( xSemaphore );
    }

    return lRetVal;
}

static inline osStatus osSemaphoreRelease( SemaphoreHandle_t xSemaphore )
{
    osStatus xReturnStatus = osOK;

    if( xSemaphore == NULL )
    {
        xReturnStatus = osErrorParameter;
    }

    if( xSemaphoreGive( xSemaphore ) == pdFALSE )
    {
        xReturnStatus = osErrorResource;
    }

    return xReturnStatus;
}

#endif /* _CMSIS_OS_COMPAT */
