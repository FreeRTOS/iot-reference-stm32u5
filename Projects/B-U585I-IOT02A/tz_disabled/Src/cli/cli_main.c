/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2020-2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 */

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "task.h"

#include "cli.h"
#include "logging.h"

#include "lfs.h"

#include <string.h>

#define CLI_COMMAND_BUFFER_SIZE 128
#define CLI_COMMAND_OUTPUT_BUFFER_SIZE configCOMMAND_INT_MAX_OUTPUT_SIZE

static IotUARTHandle_t xUART_USB = NULL;
static uint8_t ucBuffer_UART[ CLI_COMMAND_BUFFER_SIZE ] = { 0 };
static size_t xBuffer_Index = 0;
char cOutputBuffer[ CLI_COMMAND_OUTPUT_BUFFER_SIZE ] = { 0 };

void Task_CLI( void * pvParameters )
{
    /* UART Bringup */
    xUART_USB = xLoggingGetIOHandle();
    if( xUART_USB == NULL )
    {
        LogError(( "NULL USB-UART Descriptor. Exiting.\r\n" ));
        vTaskDelete( NULL );
    }

    /* FreeRTOS CLI bringup */
    FreeRTOS_CLIRegisterCommand( &xCommandDef_Configure );

    LogInfo(( "Starting CLI...\r\n" ));
    uint8_t ucByteIn = 0;
    int32_t lReadStatus = 0;
    int32_t lioctlStatus = 0;
    int32_t lNBytesRead = 0;
    size_t xNBytesOut = 0;

    /* Start an async uart read */


    while( 1 )
    {
        lReadStatus = iot_uart_read_sync( xUART_USB, &ucByteIn, 1 );
        lioctlStatus = iot_uart_ioctl( xUART_USB, eGetRxNoOfbytes, &lNBytesRead );
        if( lReadStatus == IOT_UART_SUCCESS && lioctlStatus == IOT_UART_SUCCESS && lNBytesRead > 0 )
        {
            iot_uart_write_sync( xUART_USB, &ucByteIn, 1);
            switch( ucByteIn )
            {
                case '\r':
                case '\n':
                    ucBuffer_UART[xBuffer_Index] = '\0';
                    while( pdTRUE == FreeRTOS_CLIProcessCommand( ucBuffer_UART, cOutputBuffer, CLI_COMMAND_OUTPUT_BUFFER_SIZE ) )
                    {
                        xNBytesOut = strnlen( (char*)cOutputBuffer, CLI_COMMAND_OUTPUT_BUFFER_SIZE );
                        iot_uart_write_sync( xUART_USB, cOutputBuffer, xNBytesOut );
                    }
                    /* Flush remaining */
                    xNBytesOut = strnlen( (char*)cOutputBuffer, CLI_COMMAND_OUTPUT_BUFFER_SIZE );
                    iot_uart_write_sync( xUART_USB, cOutputBuffer, xNBytesOut );
                    cOutputBuffer[0] = '\0';

                    xBuffer_Index = 0;
                    break;

                default:
                    if( xBuffer_Index < CLI_COMMAND_BUFFER_SIZE - 1 )
                    {
                        ucBuffer_UART[xBuffer_Index++] = ucByteIn;
                    }
                    break;
            }
        }

        vTaskDelay( pdMS_TO_TICKS( 1 ) );
    }
}
