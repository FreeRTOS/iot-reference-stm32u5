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

/*-----------------------------------------------------------*/

/* Dimensions the arrays into which print messages are created. */
#define dlMAX_PRINT_STRING_LENGTH       4096    /* maximum length of any single log line */
#define dlLOGGING_STREAM_LENGTH         4096    /* how many bytes to accept for logging before blocking */
#define dlLOGGING_HW_FIFO_LENGTH        8       /* How many bytes at a time can be inserted into the hardware fifo */
#define LOGGING_LINE_ENDING             "\r\n\0"

#define TASK_NOTIFY_UART_DONE_IDX       2

int _write( int fd, const void * buffer, unsigned int count );

static volatile StreamBufferHandle_t xLogStream = NULL;
static TaskHandle_t xLoggingTaskHandle = NULL;

#ifdef LOGGING_OUTPUT_UART
extern UART_HandleTypeDef huart1;
#endif

static void vUartTransmitDoneCallback( UART_HandleTypeDef * xUart )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ( void ) xUart;

    configASSERT( xLoggingTaskHandle != NULL );

    if( xLoggingTaskHandle != NULL )
    {
        vTaskNotifyGiveIndexedFromISR( xLoggingTaskHandle,
                                       TASK_NOTIFY_UART_DONE_IDX,
                                       &xHigherPriorityTaskWoken );
    }

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

static void vLoggingThread( void * pxThreadParameters )
{
    (void) pxThreadParameters;

    char cOutBuffer[ dlLOGGING_HW_FIFO_LENGTH ];
    BaseType_t xRecvResult = 0;
    HAL_StatusTypeDef xHalStatus;

    HAL_UART_RegisterCallback( &huart1, HAL_UART_TX_COMPLETE_CB_ID, vUartTransmitDoneCallback );

    while( 1 )
    {
        /* Blocking read up to cOutBuffer size on xStreamBuffer */
        xRecvResult = xStreamBufferReceive( xLogStream, cOutBuffer, dlLOGGING_HW_FIFO_LENGTH, pdMS_TO_TICKS( 4 ) );
        if( xRecvResult > 0 )
        {
            xHalStatus = HAL_UART_Transmit_IT( &huart1, ( uint8_t * ) cOutBuffer, xRecvResult );

            configASSERT( xHalStatus == HAL_OK );

            uint32_t ulWaitRslt = ulTaskNotifyTakeIndexed( TASK_NOTIFY_UART_DONE_IDX, pdTRUE, pdMS_TO_TICKS( 1000 ) );

            /* Assert if vUartTransmitDoneCallback did not get called within the timeout */
            configASSERT( ulWaitRslt != 0 );
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
        (void) HAL_UART_Transmit( &huart1, ( uint8_t * ) cOutBuffer, xNumBytes, 10 * 1000);
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
    ( void ) HAL_UART_Transmit( &huart1, (uint8_t *)buffer, count, 100000 );
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
#ifdef LOGGING_OUTPUT_UART
	MX_USART1_UART_Init();
#endif

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
                     const char * const     pcFunctionName,
                     const unsigned long    ulLineNumber,
                     const char * const     pcFormat,
                     ... )
{
    int32_t iLengthHeader = -1;
    int32_t iLengthMessage = -1;
    va_list args;
    const char * pcTaskName;
    static const char * pcNoTask = "None";
    char * pcPrintString = pvPortMalloc( dlMAX_PRINT_STRING_LENGTH + sizeof( LOGGING_LINE_ENDING ) );

    if( pcPrintString != NULL )
    {

        /* Additional info to place at the start of the log. */
        if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
        {
            pcTaskName = pcTaskGetName( NULL );
        }
        else
        {
            pcTaskName = pcNoTask;
        }

        /* Print Header
         * [LOGLEVEL] [taskName] functionName:line
         */
        iLengthHeader = snprintf( pcPrintString,
                                  dlMAX_PRINT_STRING_LENGTH,
                                  "[%s] [%s] %10lu %s:%lu --- ",
                                  pcLogLevel,
                                  pcTaskName,
                                  ( unsigned long ) xTaskGetTickCount() / portTICK_PERIOD_MS,
                                  pcFunctionName,
                                  ulLineNumber );

        configASSERT( iLengthHeader > 0 );

        /* There are a variable number of parameters. */
        va_start( args, pcFormat );
        iLengthMessage = vsnprintf( &pcPrintString[ iLengthHeader ],
                                    ( dlMAX_PRINT_STRING_LENGTH - iLengthHeader ),
                                    pcFormat,
                                    args );
        va_end( args );

        configASSERT( iLengthMessage > 0 );
        configASSERT( ( iLengthHeader + iLengthMessage ) <  dlMAX_PRINT_STRING_LENGTH );

        ( void ) strncpy( &pcPrintString[ iLengthHeader + iLengthMessage ],
                          LOGGING_LINE_ENDING,
                          dlMAX_PRINT_STRING_LENGTH + sizeof( LOGGING_LINE_ENDING ) - iLengthHeader - iLengthMessage );

        vSendLogMessage( ( void * ) pcPrintString, iLengthHeader + iLengthMessage + sizeof( LOGGING_LINE_ENDING ) );
        vPortFree(pcPrintString);
    }
}



/*-----------------------------------------------------------*/

void vLoggingDeInit( void )
{

}
