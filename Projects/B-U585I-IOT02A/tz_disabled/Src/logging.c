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

/* Standard includes. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

/* Project Includes */
#include "logging.h"
#include "main.h"

/* CommonIO Includes */
#include "iot_uart.h"

/*-----------------------------------------------------------*/
/* Dimensions the arrays into which print messages are created. */
#define dlMAX_PRINT_STRING_LENGTH       256     /* maximum length of any single log line */
#define dlLOGGING_STREAM_LENGTH         4096    /* how many bytes to accept for logging before blocking */
#define dlLOGGING_HW_FIFO_LENGTH        8       /* How many bytes at a time can be inserted into the hardware fifo */
#define LOGGING_LINE_ENDING             "\r\n"
#define dlMAX_LOG_LINE_LENGTH           dlMAX_PRINT_STRING_LENGTH - sizeof( LOGGING_LINE_ENDING )

#define TASK_NOTIFY_UART_DONE_IDX       2

static volatile StreamBufferHandle_t xLogStream = NULL;
static TaskHandle_t xLoggingTaskHandle = NULL;
static IotUARTHandle_t xConsoleUart = NULL;

int _write( int fd, const void * buffer, unsigned int count );

static void vLoggingThread( void * pxThreadParameters )
{
    (void) pxThreadParameters;

    char cOutBuffer[ dlLOGGING_HW_FIFO_LENGTH ];
    BaseType_t xRecvResult = 0;
    uint32_t xWriteStatus;

    while( 1 )
    {
        /* Blocking read up to cOutBuffer size on xStreamBuffer */
        xRecvResult = xStreamBufferReceive( xLogStream, cOutBuffer, dlLOGGING_HW_FIFO_LENGTH, pdMS_TO_TICKS( 4 ) );
        if( xRecvResult > 0 )
        {
            xWriteStatus = iot_uart_write_sync( xConsoleUart, ( uint8_t * ) cOutBuffer, xRecvResult );

            configASSERT( IOT_UART_SUCCESS == HAL_OK );
        }
    }
}

/* Should only be called during an assert with the scheduler suspended. */
void vDyingGasp( void )
{
    char cOutBuffer[ dlLOGGING_HW_FIFO_LENGTH ];
    BaseType_t xNumBytes = 0;

    do
    {
        xNumBytes = xStreamBufferReceiveFromISR( xLogStream, cOutBuffer, dlLOGGING_HW_FIFO_LENGTH, 0 );
        iot_uart_write_sync( xConsoleUart, ( uint8_t * ) cOutBuffer, xNumBytes );
    }
    while( xNumBytes != 0 );
}

/*
 * Blocking write function for early printing
 * PRE: must be called when scheduler is not running.
 */
static void vSendLogMessageEarly( const void * buffer, unsigned int count )
{
    configASSERT( xTaskGetSchedulerState() != taskSCHEDULER_RUNNING );

#ifdef LOGGING_OUTPUT_ITM
    uint32_t i = 0;
    for(unsigned int i = 0; i < len; i++)
    {
        (void) ITM_SendChar( lineOutBuf[ i ] );
    }
#endif


#ifdef LOGGING_OUTPUT_UART
    /* blocking write to UART */
    iot_uart_write_sync( xConsoleUart, (uint8_t *)buffer, count );
#endif
}

static void vSendLogMessage( const void * buffer, unsigned int count )
{
    if( xTaskGetSchedulerState() != taskSCHEDULER_RUNNING )
    {
        vSendLogMessageEarly( buffer, count );
    }
    else if( xPortIsInsideInterrupt() == pdTRUE )
    {
        UBaseType_t uxContext;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        configASSERT( xLogStream != NULL );

        /* Enter critical section to preserve ordering of log messages */
        uxContext = taskENTER_CRITICAL_FROM_ISR();

        ( void ) xStreamBufferSendFromISR( xLogStream, buffer, count, &xHigherPriorityTaskWoken );

        taskEXIT_CRITICAL_FROM_ISR( uxContext );

        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
    else
    {
        taskENTER_CRITICAL();

        ( void ) xStreamBufferSend( xLogStream, buffer, count, 0 );

        taskEXIT_CRITICAL();
    }
}

void vLoggingInit( void )
{
    int32_t status = IOT_UART_SUCCESS;

    xConsoleUart = iot_uart_open( 0 );
    configASSERT( xConsoleUart != NULL );
    IotUARTConfig_t xConfig =
    {
        .ulBaudrate    = 115200,
        .xParity      = UART_PARITY_NONE,
        .ucWordlength  = UART_WORDLENGTH_8B,
        .xStopbits    = UART_STOPBITS_1,
        .ucFlowControl = UART_HWCONTROL_NONE
    };
    status = iot_uart_ioctl( xConsoleUart, eUartSetConfig, &xConfig );
    configASSERT( status == IOT_UART_SUCCESS );

    xLogStream = xStreamBufferCreate( dlLOGGING_STREAM_LENGTH, dlLOGGING_HW_FIFO_LENGTH );

	BaseType_t xResult = xTaskCreate( vLoggingThread,
	                                  "logger",
	                                  1024,
	                                  NULL,
	                                  30,
	                                  &xLoggingTaskHandle );

	 configASSERT( xResult != pdFALSE );
	 configASSERT( xLoggingTaskHandle != NULL );
}

/*-----------------------------------------------------------*/

void vLoggingPrintf( const char * const     pcLogLevel,
                     const char * const     pcFileName,
                     const unsigned long    ulLineNumber,
                     const char * const     pcFormat,
                     ... )
{
    uint32_t ulLenTotal = 0;
    int32_t lLenPart = -1;
    va_list args;
    const char * pcTaskName = NULL;
    char pcPrintString[ dlMAX_PRINT_STRING_LENGTH ];
    pcPrintString[ 0 ] = '\0';

    /* Additional info to place at the start of the log line */
    if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
    {
        pcTaskName = pcTaskGetName( NULL );
    }
    else
    {
        pcTaskName = "None";
    }

    lLenPart = snprintf( pcPrintString,
                         dlMAX_LOG_LINE_LENGTH,
                         "<%-3.3s> %8lu [%-10.10s] ",
                         pcLogLevel,
                         ( ( unsigned long ) xTaskGetTickCount() / portTICK_PERIOD_MS ) & 0xFFFFFF,
                         pcTaskName );

    configASSERT( lLenPart > 0 );

    if( lLenPart < dlMAX_LOG_LINE_LENGTH )
    {
        ulLenTotal = lLenPart;
    }
    else
    {
        ulLenTotal = dlMAX_LOG_LINE_LENGTH;
    }

    /* There are a variable number of parameters. */
    va_start( args, pcFormat );
    lLenPart = vsnprintf( &pcPrintString[ ulLenTotal ],
                          ( dlMAX_LOG_LINE_LENGTH - ulLenTotal ),
                          pcFormat,
                          args );
    va_end( args );

    configASSERT( lLenPart > 0 );

    if( lLenPart + ulLenTotal < dlMAX_LOG_LINE_LENGTH )
    {
        ulLenTotal += lLenPart;
    }
    else
    {
        ulLenTotal = dlMAX_LOG_LINE_LENGTH;
    }

    /* remove any \r\n\0 characters at the end of the message */
    while( ulLenTotal > 0 &&
           ( pcPrintString[ ulLenTotal - 1 ] == '\r' ||
             pcPrintString[ ulLenTotal - 1 ] == '\n' ||
             pcPrintString[ ulLenTotal - 1 ] == '\0' ) )
    {
        pcPrintString[ ulLenTotal - 1 ] = '\0';
        ulLenTotal--;
    }

    if( pcFileName != NULL &&
        ulLineNumber > 0 &&
        ulLenTotal < dlMAX_LOG_LINE_LENGTH )
    {
        /* Add the trailer including file name and line number */
        lLenPart = snprintf( &pcPrintString[ ulLenTotal ],
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

    ( void ) strncpy( &pcPrintString[ ulLenTotal ],
                      LOGGING_LINE_ENDING,
                      dlMAX_PRINT_STRING_LENGTH - ulLenTotal );

    ulLenTotal += sizeof( LOGGING_LINE_ENDING );

    vSendLogMessage( ( void * ) pcPrintString, ulLenTotal );
}

/*-----------------------------------------------------------*/

IotUARTHandle_t xLoggingGetIOHandle( void )
{
	return xConsoleUart;
}

/*-----------------------------------------------------------*/

void vLoggingDeInit( void )
{

}

