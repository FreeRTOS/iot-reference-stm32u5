/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

#ifndef _MBEDTLS_UTILS_H
#define _MBEDTLS_UTILS_H

#include "mbedtls/error.h"
#include <string.h>
#include <stdarg.h>

/**
 * @brief Utility for converting the high-level code in an mbedTLS error to string,
 * if the code-contains a high-level code; otherwise, using a default string.
 */
#ifndef mbedtlsHighLevelCodeOrDefault
#define mbedtlsHighLevelCodeOrDefault( mbedTlsCode )       \
    ( mbedtls_high_level_strerr( mbedTlsCode ) != NULL ) ? \
    mbedtls_high_level_strerr( mbedTlsCode ) : ( const char * ) "<No-High-Level-Code>"
#endif /* mbedtlsHighLevelCodeOrDefault */

/**
 * @brief Utility for converting the level-level code in an mbedTLS error to string,
 * if the code-contains a level-level code; otherwise, using a default string.
 */
#ifndef mbedtlsLowLevelCodeOrDefault
#define mbedtlsLowLevelCodeOrDefault( mbedTlsCode )       \
    ( mbedtls_low_level_strerr( mbedTlsCode ) != NULL ) ? \
    mbedtls_low_level_strerr( mbedTlsCode ) : ( const char * ) "<No-Low-Level-Code>"
#endif /* mbedtlsLowLevelCodeOrDefault */

#define MBEDTLS_MSG_IF_ERROR( lError, pMessage )                  \
    do                                                            \
    {                                                             \
        if( lError < 0 ) {                                        \
            LogError( pMessage " %s : %s.",                       \
                      mbedtlsHighLevelCodeOrDefault( lError ),    \
                      mbedtlsLowLevelCodeOrDefault( lError ) ); } \
    } while( 0 )

#define MBEDTLS_LOG_IF_ERROR( lError, pFormatString, ... )        \
    do                                                            \
    {                                                             \
        if( lError < 0 ) {                                        \
            LogError( pFormatString " %s : %s.", __VA_ARGS__,     \
                      mbedtlsHighLevelCodeOrDefault( lError ),    \
                      mbedtlsLowLevelCodeOrDefault( lError ) ); } \
    } while( 0 )

#endif /* _MBEDTLS_UTILS_H */
