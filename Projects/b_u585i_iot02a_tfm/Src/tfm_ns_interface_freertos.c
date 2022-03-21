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

#include "FreeRTOS.h"
#include "semphr.h"
#include <stdint.h>
#include "tfm_ns_interface.h"

#if ( configSUPPORT_STATIC_ALLOCATION == 1 && configSUPPORT_DYNAMIC_ALLOCATION == 0 )

/*
 * In the static allocation, the RAM is required to hold the semaphore's
 * state.
 */
static StaticSemaphore_t xNsIntfMutexBuffer = { 0 };
#endif

static SemaphoreHandle_t xNsIntfMutex = NULL;

int32_t ns_interface_lock_init( void )
{
    int32_t lReturn = -1;

#if ( configSUPPORT_STATIC_ALLOCATION == 1 && configSUPPORT_DYNAMIC_ALLOCATION == 0 )
    xNsIntfMutex = xSemaphoreCreateMutexStatic( &xNsIntfMutexBuffer );
#else
    xNsIntfMutex = xSemaphoreCreateMutex();
#endif

    if( xNsIntfMutex != NULL )
    {
        lReturn = 0;
    }

    return lReturn;
}


int32_t tfm_ns_interface_dispatch( veneer_fn fn,
                                   uint32_t arg0,
                                   uint32_t arg1,
                                   uint32_t arg2,
                                   uint32_t arg3 )
{
    int32_t lResult = -1;

    configASSERT( xNsIntfMutex != NULL );

    if( xSemaphoreTake( xNsIntfMutex, portMAX_DELAY ) == pdTRUE )
    {
        lResult = fn( arg0, arg1, arg2, arg3 );

        xSemaphoreGive( xNsIntfMutex );
    }

    return lResult;
}
