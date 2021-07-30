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

/* Non-cmsis net_os.c implementation for STM32 Network Library */


/* STM32 NetworkLib includes */
#include "net_connect.h"
#include "net_internals.h"

/* FreeRTOS Includes */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static SemaphoreHandle_t net_mutex[NET_LOCK_NUMBER] = { 0 };

void net_init_locks( void )
{
    for( uint32_t i = 0; i < NET_LOCK_NUMBER; i++ )
    {
        net_mutex[ i ] = NULL;
        net_mutex[ i ] = xSemaphoreCreateMutex();
        configASSERT( net_mutex[ i ] != NULL );
        (void) xSemaphoreGive( net_mutex[ i ] );
    }
}

void net_destroy_locks( void )
{
    for( uint32_t i = 0; i < NET_LOCK_NUMBER; i++ )
    {
        vSemaphoreDelete( net_mutex[ i ] );
        net_mutex[ i ] = NULL;
    }
}

void net_lock( int32_t sock, uint32_t timeout_ms )
{
    TickType_t xTimeout;

    configASSERT( xPortIsInsideInterrupt() == 0 );

    /* validate parameters */
    configASSERT( sock >= 0 );

    configASSERT( net_mutex[ sock ] != NULL );

    if( timeout_ms == NET_OS_WAIT_FOREVER )
    {
        xTimeout = portMAX_DELAY;
    }
    else
    {
        xTimeout = pdMS_TO_TICKS( timeout_ms );
    }

    if( xSemaphoreTake( net_mutex[ sock ], xTimeout ) != pdTRUE )
    {
        configASSERT( pdFALSE );
    }
}

void net_unlock( int32_t sock )
{
    configASSERT( xPortIsInsideInterrupt() == 0 );
    /* validate parameters */
    configASSERT( sock >= 0 );

    configASSERT( net_mutex[ sock ] != NULL );

    if( xSemaphoreGive( net_mutex[ sock ] ) != pdTRUE )
    {
        configASSERT( pdFALSE );
    }
}

void net_lock_nochk( int32_t sock, uint32_t timeout_ms )
{
    TickType_t xTimeout;

    configASSERT( xPortIsInsideInterrupt() == 0 );

    /* validate parameters */
    configASSERT( sock >= 0 );

    configASSERT( net_mutex[ sock ] != NULL );

    if( timeout_ms == NET_OS_WAIT_FOREVER )
    {
        xTimeout = portMAX_DELAY;
    }
    else
    {
        xTimeout = pdMS_TO_TICKS( timeout_ms );
    }

    ( void ) xSemaphoreTake( net_mutex[ sock ], xTimeout );
}

void net_unlock_nochk( int32_t sock )
{
    configASSERT( xPortIsInsideInterrupt() == 0 );
    /* validate parameters */
    configASSERT( sock >= 0 );

    configASSERT( net_mutex[ sock ] != NULL );
    ( void ) xSemaphoreGive( net_mutex[ sock ] );
}

void * net_calloc( size_t n, size_t m)
{
    void * pvMem = pvPortMalloc( n * m );
    if( pvMem != NULL )
    {
        ( void ) memset( pvMem, 0, n * m );
    }
    return pvMem;
}

/* Copied from heap_4.c */
typedef struct A_BLOCK_LINK
{
    struct A_BLOCK_LINK * pxNextFreeBlock; /*<< The next free block in the list. */
    size_t xBlockSize;                     /*<< The size of the free block. */
} BlockLink_t;
static const size_t xHeapStructSize = ( sizeof( BlockLink_t ) + ( ( size_t ) ( portBYTE_ALIGNMENT - 1 ) ) ) & ~( ( size_t ) portBYTE_ALIGNMENT_MASK );


void * net_realloc( void * pvMemIn, size_t size )
{
    void * pvMem = NULL;

    if( pvMemIn != NULL )
    {
        /* Determine existing allocation size */
        BlockLink_t * pxLink = ( BlockLink_t * ) ( ( ( uint8_t * ) pvMemIn ) - ( ( uint8_t * ) xHeapStructSize ) );

        /* New size is the same */
        if( pxLink->xBlockSize == size )
        {
            pvMem = pvMemIn;
        }
        /* New block size is smaller than current block */
        else if( pxLink->xBlockSize > size )
        {
            pvMem = pvPortMalloc( size );
            if( pvMem != NULL )
            {
                ( void ) memcpy( pvMem, pvMemIn, size );
                vPortFree( pvMemIn );
            }
        }
        /* New block size is larger than current block */
        else
        {
            pvMem = pvPortMalloc( size );

            if( pvMem != NULL )
            {
                ( void ) memcpy( pvMem, pvMemIn, pxLink->xBlockSize );
                vPortFree( pvMemIn );
            }
        }
    }
    else
    {
        pvMem = pvPortMalloc( size );
    }
    return pvMem;
}
