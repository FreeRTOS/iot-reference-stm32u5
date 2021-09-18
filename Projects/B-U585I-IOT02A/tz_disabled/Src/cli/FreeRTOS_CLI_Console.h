/*
 * Copyright (C) 2017 - 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 *
 */

#ifndef FREERTOS_CLI_CONSOLE_H
#define FREERTOS_CLI_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Defines the interface for different console implementations. Interface
 * defines the contract of how bytes are transferred between console
 * and FreeRTOS CLI.
 */
typedef struct xConsoleIO
{
    /**
     * Function reads input bytes from the console into a finite length buffer upto the length
     * requested by the second parameter. It returns the number of bytes read which can
     * be less than or equal to the requested value. If no input bytes are available,
     * function can either block or return immediately with 0 bytes read. If there is an error for the
     * read, it returns negative error code.
     * FreeRTOS CLI uses this function to read the command string from input console.
     * The API is not thread-safe.
     *
     */
    const int32_t ( * read )( char * const buffer,
                        	  uint32_t length );

    const int32_t ( * read_timeout )( char * const buffer,
                                      uint32_t length,
							          TickType_t xTimeout );

    const int32_t ( * readline ) ( char * * const bufferPtr );

    /**
     * Function writes the output of a finite length buffer to the console. If the buffer is a null
     * terminated string, the entire length of the buffer (including null characters) will be sent
     * to the serial port.
     */
    const void ( * write )( const void * const pvBuffer,
                            uint32_t length );

    /**
     * Function writes a null terminated string to the console.
     */
    const void ( * print ) ( const char * const pcString );

    const void ( * lock ) ( void );
    const void ( * unlock ) ( void );
} ConsoleIO_t;

#endif /* ifndef FREERTOS_CLI_CONSOLE_H */
