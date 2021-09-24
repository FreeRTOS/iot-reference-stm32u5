/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2017-2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
#include "cli_prv.h"
#include "stream_buffer.h"

#include <string.h>

extern ConsoleIO_t xConsoleIO;
extern BaseType_t xInitConsoleUart( void );

void Task_CLI( void * pvParameters )
{
    FreeRTOS_CLIRegisterCommand( &xCommandDef_conf );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_pki );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_ps );

    char * pcCommandBuffer = NULL;

    if( xInitConsoleUart() == pdTRUE )
    {
        for( ; ; )
        {
            /* Read a line of input */
            int32_t lLen = xConsoleIO.readline( &pcCommandBuffer );
            if( pcCommandBuffer != NULL &&
                lLen > 0 )
            {
                FreeRTOS_CLIProcessCommand( &xConsoleIO, pcCommandBuffer );
            }
        }
    }
    else
    {
        LogError( "Failed to initialize UART console." );
        vTaskDelete( NULL );
    }
}
