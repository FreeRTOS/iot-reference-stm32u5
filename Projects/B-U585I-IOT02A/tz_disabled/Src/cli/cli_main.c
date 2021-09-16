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
#include "cli_prv.h"
#include "stream_buffer.h"

#include <string.h>

#define CLI_COMMAND_BUFFER_SIZE 128
#define CLI_COMMAND_OUTPUT_BUFFER_SIZE configCOMMAND_INT_MAX_OUTPUT_SIZE

static char ucCommandBuffer[ CLI_COMMAND_BUFFER_SIZE ] = { 0 };

extern ConsoleIO_t xConsoleIODesc;
extern BaseType_t xInitConsoleUart( void );

extern volatile BaseType_t xPartialCommand;

static int32_t readline( ConsoleIO_t * pxConsoleIO,
					     char * const pcInputBuffer,
						 uint32_t xInputBufferLen )
{
	int32_t lBytesWritten = 0;

	//TODO timeout struct here to timeout xPartialCommand flag

	uint32_t ulWriteIdx = 0;
	BaseType_t xFoundEOL = pdFALSE;

	/* Ensure null termination */
	pcInputBuffer[ xInputBufferLen - 1 ] = '\0';

	pxConsoleIO ->write( "> ", 2 );

	while( ulWriteIdx < ( xInputBufferLen - 1 ) &&
		   xFoundEOL == pdFALSE )
	{
		if( pxConsoleIO->read_timeout( &( pcInputBuffer[ ulWriteIdx ] ), 1, portMAX_DELAY ) )
		{
            xPartialCommand = pdTRUE;

            switch( pcInputBuffer[ ulWriteIdx ] )
            {
            case '\n':
            case '\r':
            case '\00':
                /* If we have an actual string to report, do so */
                if( ulWriteIdx > 0 )
                {
                    /* Null terminate the output string */
                    pcInputBuffer[ ulWriteIdx ] = '\0';
                    lBytesWritten = ulWriteIdx;
                    xFoundEOL = pdTRUE;
                    xPartialCommand = pdFALSE;

                    /* Turn every line ending into an \r\n */
                    ( void ) pxConsoleIO->write( "\r\n", 2 );
                }
                /* Otherwise, drop the single \r or \n character */
                else
                {
                    pcInputBuffer[ ulWriteIdx ] = '\0';
                }
                break;
                /* Handle backspace / delete characters */
            case '\b':
            case '\x7F': /* ASCII DEL character */
                if( ulWriteIdx > 0 )
                {
                    /* Erase current character (del or backspace) and previous character */
                    pcInputBuffer[ ulWriteIdx ] = '\0';
                    pcInputBuffer[ ulWriteIdx - 1 ] = '\0';
                    ulWriteIdx--;
                    ( void ) pxConsoleIO->print( "\b \b" );
                }
                break;
                /* Otherwise consume the character as normal */
            default:
                ( void ) pxConsoleIO ->write( &( pcInputBuffer[ ulWriteIdx ] ), 1 );
                ulWriteIdx++;
                break;
            }
		}
	}
	return lBytesWritten;
}

void Task_CLI( void * pvParameters )
{
    FreeRTOS_CLIRegisterCommand( &xCommandDef_conf );
//    FreeRTOS_CLIRegisterCommand( &xCommandDef_pki );

    if( xInitConsoleUart() == pdTRUE )
    {
        for( ; ; )
        {
            /* Read a line of input */
            int32_t lLen = readline( &xConsoleIODesc, ucCommandBuffer, CLI_COMMAND_BUFFER_SIZE );
            if( lLen > 0 )
            {
                while( FreeRTOS_CLIProcessCommand( ucCommandBuffer, &xConsoleIODesc ) == pdTRUE );
            }
        }
    }
}
