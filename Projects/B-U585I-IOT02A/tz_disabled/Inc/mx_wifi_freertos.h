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
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 */

#ifndef MX_WIFI_FREERTOS_H
#define MX_WIFI_FREERTOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#define MX_WIFI_MALLOC  pvPortMalloc
#define MX_WIFI_FREE    vPortFree

// #if (MX_WIFI_NETWORK_BYPASS_MODE == 1)
// /* Definition for  LwIP usage  */

// #include "lwip/netdb.h"
// typedef struct pbuf mx_buf_t;

// #define MX_NET_BUFFER_ALLOC(len)                                            pbuf_alloc(PBUF_RAW,len,PBUF_POOL)
// #define MX_NET_BUFFER_FREE(p)                                               pbuf_free(p)
// #define MX_NET_BUFFER_HIDE_HEADER(p,n)                                      pbuf_header(p,(int16_t)-(n))
// #define MX_NET_BUFFER_PAYLOAD(p)                                            p->payload
// #define MX_NET_BUFFER_SET_PAYLOAD_SIZE(p,size)                              pbuf_realloc(p,size)
// #define MX_NET_BUFFER_GET_PAYLOAD_SIZE(p)                                   p->len

// #endif /* MX_WIFI_NETWORK_BYPASS_MODE */

//#define OSPRIORITYNORMAL                                                    16
//#define OSPRIORITYABOVENORMAL                                               24
//#define OSPRIORITYREALTIME                                                  30
#define MX_ASSERT(A)                                configASSERT(A)

/* Mutex definitions */
#define LOCK_DECLARE(A)                             SemaphoreHandle_t A
#define LOCK_INIT(A)                                A = xSemaphoreCreateMutex(); xSemaphoreGive( A )
#define LOCK_DEINIT(A)                              vSemaphoreDelete( A ); A = NULL
static BaseType_t LOCK( SemaphoreHandle_t A )
{
    LogInfo("Wifi LOCK: %p", A);
    configASSERT( xPortIsInsideInterrupt() == 0 );
    return xSemaphoreTake( A, portMAX_DELAY );
}

static BaseType_t UNLOCK( SemaphoreHandle_t A )
{
    LogInfo("Wifi UNLOCK: %p", A);
    configASSERT( xPortIsInsideInterrupt() == 0 );
    return xSemaphoreGive( A );
}

/* Counting semaphore definitions */
#define SEM_DECLARE( A )                            SemaphoreHandle_t A
#define SEM_INIT( A, COUNT )                        A = xSemaphoreCreateCounting( COUNT, 0 )
#define SEM_DEINIT( A )                             vSemaphoreDelete( A )
static inline BaseType_t _sem_signal( SemaphoreHandle_t xSemaphore )
{
    BaseType_t xReturnValue;
    BaseType_t xHigherPriorityWoken = pdFALSE;
    if( xPortIsInsideInterrupt() )
    {
        xReturnValue = xSemaphoreGiveFromISR( xSemaphore, &xHigherPriorityWoken );
    }
    else
    {
        xReturnValue = xSemaphoreGive( xSemaphore );
    }

    if( xHigherPriorityWoken == pdTRUE )
    {
        vPortYield();
    }
    return xReturnValue;
}
#define SEM_SIGNAL( A )                             _sem_signal( A )
static inline BaseType_t _sem_wait( SemaphoreHandle_t xSemaphore, uint32_t timeout )
{
    BaseType_t xReturnValue;
    BaseType_t xHigherPriorityWoken = pdFALSE;
    if( xPortIsInsideInterrupt() )
    {
        xReturnValue = xSemaphoreTakeFromISR( xSemaphore, &xHigherPriorityWoken );
    }
    else
    {
        xReturnValue = xSemaphoreTake( xSemaphore, pdMS_TO_TICKS( timeout ) );
    }

    if( xHigherPriorityWoken == pdTRUE )
    {
        vPortYield();
    }
    return xReturnValue;
}
#define SEM_WAIT( A, TIMEOUT, IDLE_FUNC )           _sem_wait( A, pdMS_TO_TICKS( TIMEOUT ) )

//TODO Should a semaphore be in the given or taken state upon creations?


/* Thread definitions */
#define THREAD_DECLARE( A )                                                 TaskHandle_t A
#define THREAD_INIT( A, THREAD_FUNC, CTX, STACKSIZE, PRIORITY )             xTaskCreate( THREAD_FUNC, #A, STACKSIZE, CTX, PRIORITY, &A )
#define THREAD_DEINIT( A )                                                  vTaskDelete( A )
#define THREAD_TERMINATE( )                                                 vTaskDelete( NULL )
#define THREAD_CONTEXT_TYPE                                                 void * const

/* FIFO / pointer queue related */
static portINLINE void * fifo_pop( QueueHandle_t xQueue, TickType_t xTimeout )
{
    void * pRetVal = NULL;

    if( xQueueReceive( xQueue, &pRetVal, xTimeout ) != pdTRUE )
    {
        /* clear pointer on failure */
        pRetVal = NULL;
    }
    return pRetVal;
}

#define FIFO_DECLARE( QUEUE )                           QueueHandle_t QUEUE
#define FIFO_INIT( QUEUE, QSIZE )                       QUEUE = xQueueCreate( QSIZE, sizeof( void * ) )
#define FIFO_PUSH( QUEUE, VALUE, TIMEOUT, IDLE_FUNC )   xQueueSend( QUEUE, &VALUE, pdMS_TO_TICKS( TIMEOUT ) )
#define FIFO_POP( QUEUE, TIMEOUT, IDLE_FUNC )           fifo_pop( QUEUE, pdMS_TO_TICKS( TIMEOUT ) )
#define FIFO_DEINIT( QUEUE )                            vQueueDelete( QUEUE )

#define WAIT_FOREVER                                    portMAX_DELAY
#define SEM_OK                                          pdTRUE
#define THREAD_OK                                       pdTRUE
#define QUEUE_OK                                        pdTRUE
#define FIFO_OK                                         pdTRUE

#define DELAYms( n )                                    vTaskDelay( pdMS_TO_TICKS( n ) )

typedef struct mx_buf
{
  uint32_t len;
  uint32_t header_len;
  uint8_t  data[1];
} mx_buf_t;

static portINLINE mx_buf_t * mx_buf_alloc( uint32_t len )
{
    mx_buf_t *pBuf = (mx_buf_t *) MX_WIFI_MALLOC( len + sizeof(mx_buf_t) - 1U );
    if( pBuf != NULL )
    {
        pBuf->len = len;
        pBuf->header_len = 0;
    }
    return pBuf;
}

#define MX_NET_BUFFER_ALLOC( len )                      mx_buf_alloc( len )
#define MX_NET_BUFFER_FREE( p )                         MX_WIFI_FREE( p )
#define MX_NET_BUFFER_HIDE_HEADER( p, n )               (p)->header_len += ( n )
#define MX_NET_BUFFER_PAYLOAD( p )                      &( p )->data[ ( p )->header_len ]
#define MX_NET_BUFFER_SET_PAYLOAD_SIZE( p, size )       ( p )->len = ( size )
#define MX_NET_BUFFER_GET_PAYLOAD_SIZE( p )             ( p )->len


#ifdef __cplusplus
}
#endif

#endif /* MX_WIFI_FREERTOS_H */
