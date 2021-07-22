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
#include "semphr.h"

/* Project Includes */
#include "logging.h"
#include "main.h"

/*-----------------------------------------------------------*/

/* Dimensions the arrays into which print messages are created. */
#define dlMAX_PRINT_STRING_LENGTH    2048

/*
 * Maximum amount of time to wait for the semaphore that protects the local
 * buffers and the serial output.
 */
#define dlMAX_SEMAPHORE_WAIT_TIME    ( pdMS_TO_TICKS( 2000UL ) )

int _write( int fd, const void * buffer, unsigned int count );

static char cPrintString[ dlMAX_PRINT_STRING_LENGTH ];
static SemaphoreHandle_t xLoggingMutex = NULL;
static SemaphoreHandle_t xWriteMutex = NULL;

#ifdef LOGGING_OUTPUT_UART
extern UART_HandleTypeDef huart1;
#endif

int _write( int fd, const void * buffer, unsigned int count )
{
	(void) fd;
	BaseType_t ret = 0;

#ifdef LOGGING_OUTPUT_ITM
    for(unsigned int i = 0; i < len; i++)
    {
        (void) ITM_SendChar(lineOutBuf[i]);
    }
#endif


#ifdef LOGGING_OUTPUT_UART
	/* blocking write to UART */
	ret = (BaseType_t) HAL_UART_Transmit( &huart1, (uint8_t *)buffer, count, 100000 );
#endif

	if( ret == 0 )
	{
		return 0;
	}
	else
	{
		return count;
	}
}

void vLoggingInit( void )
{
	xLoggingMutex = xSemaphoreCreateMutex();
	xWriteMutex = xSemaphoreCreateMutex();
	memset( &cPrintString, 0, dlMAX_PRINT_STRING_LENGTH );

#ifdef LOGGING_OUTPUT_UART
	MX_USART1_UART_Init();
#endif


	xSemaphoreGive( xLoggingMutex );
	xSemaphoreGive( xWriteMutex );
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
    const char * const pcNewline = "\r\n";
    const char * pcTaskName;
    const char * pcNoTask = "None";


    /* Additional info to place at the start of the log. */
    if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
    {
        pcTaskName = pcTaskGetName( NULL );
    }
    else
    {
        pcTaskName = pcNoTask;
    }

    configASSERT( xLoggingMutex );


    if( xSemaphoreTake( xLoggingMutex, dlMAX_SEMAPHORE_WAIT_TIME ) != pdFAIL )
    {
        /* Print Header
         * [LOGLEVEL] [taskName] functionName:line
         */
        iLengthHeader = snprintf( cPrintString,
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
        iLengthMessage = vsnprintf( &cPrintString[ iLengthHeader ],
                                    ( dlMAX_PRINT_STRING_LENGTH - iLengthHeader ),
                                    pcFormat,
                                    args );
        va_end( args );

        configASSERT( iLengthMessage > 0 );
        configASSERT( ( iLengthHeader + iLengthMessage ) <  dlMAX_PRINT_STRING_LENGTH );

        _write( 0, cPrintString, iLengthHeader + iLengthMessage );
        _write( 0, pcNewline, strlen( pcNewline ) );

        xSemaphoreGive( xLoggingMutex );
    }
}

/*-----------------------------------------------------------*/

void vLoggingDeInit( void )
{
	/* Scheduler must be suspended */
	if( xLoggingMutex != NULL &&
	    xTaskGetSchedulerState() != taskSCHEDULER_RUNNING )
	{
		vSemaphoreDelete( xLoggingMutex );

	}

    if( xWriteMutex != NULL &&
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING )
    {
        vSemaphoreDelete( xWriteMutex );
    }
}
