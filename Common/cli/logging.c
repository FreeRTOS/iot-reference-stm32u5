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

/* Standard includes. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"
#include "cli_prv.h"

/* Project Includes */
#include "logging.h"
#include "hw_defs.h"

/*-----------------------------------------------------------*/
/* todo take into account maximum cli line length */
#if ( CLI_UART_TX_STREAM_LEN < dlMAX_LOG_LINE_LENGTH )
#error "CLI_UART_TX_STREAM_LEN must be >= dlMAX_LOG_LINE_LENGTH"
#endif

volatile StreamBufferHandle_t xLogMBuf = NULL;

UART_HandleTypeDef * pxEarlyUart = NULL;

static char pcPrintBuff[ dlMAX_LOG_LINE_LENGTH ];

/* Should only be called during an assert with the scheduler suspended. */
void vDyingGasp( void )
{
    BaseType_t xNumBytes = 0;

    /* Pet the watchdog so that the message is not lost */
    vPetWatchdog();

    pxEarlyUart = vInitUartEarly();

    do
    {
        xNumBytes = xMessageBufferReceiveFromISR( xLogMBuf, pcPrintBuff, dlMAX_PRINT_STRING_LENGTH, 0 );
        ( void ) HAL_UART_Transmit( pxEarlyUart, ( uint8_t * ) pcPrintBuff, xNumBytes, 10 * 1000 );
        ( void ) HAL_UART_Transmit( pxEarlyUart, ( uint8_t * ) "\r\n", 2, 10 * 1000 );

        /* Pet the watchdog */
        vPetWatchdog();
    }
    while( xNumBytes != 0 );

    HAL_GPIO_WritePin( LED_RED_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET );
    HAL_GPIO_WritePin( LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET );
}

/*
 * Blocking write function for early printing
 * PRE: must be called when scheduler is not running.
 */
static void vSendLogMessageEarly( const char * buffer,
                                  unsigned int count )
{
    configASSERT( xTaskGetSchedulerState() != taskSCHEDULER_RUNNING );

#ifdef LOGGING_OUTPUT_ITM
    uint32_t i = 0;

    for( unsigned int i = 0; i < len; i++ )
    {
        ( void ) ITM_SendChar( lineOutBuf[ i ] );
    }
#endif


#ifdef LOGGING_OUTPUT_UART
    /* blocking write to UART */
    ( void ) HAL_UART_Transmit( pxEarlyUart, ( uint8_t * ) buffer, count, 100000 );
    ( void ) HAL_UART_Transmit( pxEarlyUart, ( uint8_t * ) "\r\n", 2, 100000 );
#endif
}

void vInitLoggingEarly( void )
{
    pxEarlyUart = vInitUartEarly();
    vSendLogMessageEarly( "\r\n", 2 );
}

static void vSendLogMessage( const char * buffer,
                             unsigned int count )
{
    if( xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED )
    {
        vSendLogMessageEarly( buffer, count );
    }
    else if( xPortIsInsideInterrupt() == pdTRUE )
    {
        UBaseType_t uxContext;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        configASSERT( xLogMBuf != NULL );

        /* Enter critical section to preserve ordering of log messages */
        uxContext = taskENTER_CRITICAL_FROM_ISR();
        size_t xSpaceAvailable = xMessageBufferSpaceAvailable( xLogMBuf );

        if( xSpaceAvailable > sizeof( size_t ) )
        {
            xSpaceAvailable -= sizeof( size_t );

            if( xSpaceAvailable < ( count + sizeof( size_t ) ) )
            {
                ( void ) xMessageBufferSendFromISR( xLogMBuf, buffer, xSpaceAvailable - sizeof( size_t ), &xHigherPriorityTaskWoken );
            }
            else
            {
                ( void ) xMessageBufferSendFromISR( xLogMBuf, buffer, count, &xHigherPriorityTaskWoken );
            }
        }

        taskEXIT_CRITICAL_FROM_ISR( uxContext );

        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
    else
    {
        configASSERT( xLogMBuf != NULL );
        size_t xSpaceAvailable = xMessageBufferSpaceAvailable( xLogMBuf );

        if( xSpaceAvailable > sizeof( size_t ) )
        {
            xSpaceAvailable -= sizeof( size_t );

            if( xSpaceAvailable < count )
            {
                ( void ) xMessageBufferSend( xLogMBuf, buffer, xSpaceAvailable, 0 );
            }
            else
            {
                ( void ) xMessageBufferSend( xLogMBuf, buffer, count, 0 );
            }
        }
    }
}

void vLoggingInit( void )
{
    xLogMBuf = xMessageBufferCreate( dlLOGGING_STREAM_LENGTH );
}

/*-----------------------------------------------------------*/

void vLoggingPrintf( const char * const pcLogLevel,
                     const char * const pcFileName,
                     const unsigned long ulLineNumber,
                     const char * const pcFormat,
                     ... )
{
    uint32_t ulLenTotal = 0;
    int32_t lLenPart = -1;
    va_list args;
    const char * pcTaskName = NULL;
    BaseType_t xSchedulerWasSuspended = pdFALSE;

    /* Additional info to place at the start of the log line */
    if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
    {
        pcTaskName = pcTaskGetName( NULL );
    }
    else
    {
        pcTaskName = "None";
    }

    if( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING )
    {
        xSchedulerWasSuspended = pdTRUE;
        /* Suspend the scheduler to access pcPrintBuff */
        vTaskSuspendAll();
    }

    pcPrintBuff[ 0 ] = '\0';
    lLenPart = snprintf( pcPrintBuff,
                         dlMAX_PRINT_STRING_LENGTH,
                         "<%-3.3s> %8lu [%-10.10s] ",
                         pcLogLevel,
                         ( ( unsigned long ) xTaskGetTickCount() / portTICK_PERIOD_MS ) & 0xFFFFFF,
                         pcTaskName );

    configASSERT( lLenPart > 0 );

    if( lLenPart < dlMAX_PRINT_STRING_LENGTH )
    {
        ulLenTotal = lLenPart;
    }
    else
    {
        ulLenTotal = dlMAX_PRINT_STRING_LENGTH;
    }

    if( ulLenTotal < dlMAX_PRINT_STRING_LENGTH )
    {
        /* There are a variable number of parameters. */
        va_start( args, pcFormat );
        lLenPart = vsnprintf( &pcPrintBuff[ ulLenTotal ],
                              ( dlMAX_PRINT_STRING_LENGTH - ulLenTotal ),
                              pcFormat,
                              args );
        va_end( args );

        configASSERT( lLenPart > 0 );

        if( lLenPart + ulLenTotal < dlMAX_PRINT_STRING_LENGTH )
        {
            ulLenTotal += lLenPart;
        }
        else
        {
            ulLenTotal = dlMAX_PRINT_STRING_LENGTH;
        }
    }

    /* remove any \r\n\0 characters at the end of the message */
    while( ulLenTotal > 0 &&
           ( pcPrintBuff[ ulLenTotal - 1 ] == '\r' ||
             pcPrintBuff[ ulLenTotal - 1 ] == '\n' ||
             pcPrintBuff[ ulLenTotal - 1 ] == '\0' ) )
    {
        pcPrintBuff[ ulLenTotal - 1 ] = '\0';
        ulLenTotal--;
    }

    if( ( pcFileName != NULL ) &&
        ( ulLineNumber > 0 ) &&
        ( ulLenTotal < dlMAX_LOG_LINE_LENGTH ) )
    {
        /* Add the trailer including file name and line number */
        lLenPart = snprintf( &pcPrintBuff[ ulLenTotal ],
                             ( dlMAX_LOG_LINE_LENGTH - ulLenTotal ),
                             " (%s:%lu)",
                             pcFileName,
                             ulLineNumber );

        configASSERT( lLenPart > 0 );

        if( lLenPart + ulLenTotal < dlMAX_LOG_LINE_LENGTH )
        {
            ulLenTotal += lLenPart;
        }
        else
        {
            ulLenTotal = dlMAX_LOG_LINE_LENGTH;
        }
    }

    vSendLogMessage( ( void * ) pcPrintBuff, ulLenTotal );

    if( xSchedulerWasSuspended == pdTRUE )
    {
        xTaskResumeAll();
    }
}

/*-----------------------------------------------------------*/
void vLoggingDeInit( void )
{
}
