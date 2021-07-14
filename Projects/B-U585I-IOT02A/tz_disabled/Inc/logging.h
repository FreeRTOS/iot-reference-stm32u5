/*
 * Lab-Project-coreMQTT-Agent 201215
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 * 1 tab == 4 spaces!
 */

#ifndef LOGGING_CONFIG_H
#define LOGGING_CONFIG_H

/*
 * Logging configuration.
 *
 * This example uses two function calls per logged message.  First
 * xLoggingPrintMetadata() attempts to obtain a mutex that grants it access to
 * the output port before outputting metadata about the log.  Second
 * vLoggingPrintf() writes the log message itself before releasing the mutex.
 * These are the prototypes of the functions and the definitions of the macros
 * that call them - there is one macro per severity level.
 *
 * Commenting out the implementation of a macro will omit logging at that level.
 * For example, to remove the most verbose level of logging (DEBUG) simply
 * comment out the implementation of LogDebug() making it an empty macro as
 * shown here:"#define LogDebug( message )"
 *
 * If you want to print out additional metadata then update the
 * xLoggingPrintMetadata() function prototype and implementation so it accepts
 * more parameters, then update the implementation of the macros before to pass
 * the additional information.  For example, if you want the logs to print out
 * the name of the function that called the logging macro then add an additional
 * char * parameter to xLoggingPrintMetadata() function:
 *
 * extern int xLoggingPrintMetadata( const char * const pcLevel,
 *                                   const char * const pcFunctionName ); << new parameter
 *
 * Then update the call to xLoggingPrintMetadata to pass the additional parameter:
 *
 * xLoggingPrintMetadata( "ERROR", __FUNCTION__ ); << Added __FUNCTION__ as second parameter.
 *
 * ....and of course update the implementation of xLoggingPrintMetadata() to
 * actually print the function name passed in to it.
 */
void vLoggingPrintf( const char * const pcFormatString,
                     ... );
int32_t xLoggingPrintMetadata( const char * const pcLevel );
void vLoggingInit( void );

/* See comments immediately above for instructions on changing the verboseness
 * of the logging and adding data such as the function that called the logging
 * macro to the logged output.
 */
#define LogError( message )    do { xLoggingPrintMetadata( "ERROR" ); vLoggingPrintf message; } while( 0 )
#define LogWarn( message )     do { xLoggingPrintMetadata( "WARN" ); vLoggingPrintf message; } while( 0 )
#define LogInfo( message )     do { xLoggingPrintMetadata( "INFO" ); vLoggingPrintf message; } while( 0 )
#define LogDebug( message )    /*do{ xLoggingPrintMetadata( "DEBUG" ); vLoggingPrintf message ;} while( 0 )*//* Uncomment to output debug level logging. */

#endif /* LOGGING_CONFIG_H */
