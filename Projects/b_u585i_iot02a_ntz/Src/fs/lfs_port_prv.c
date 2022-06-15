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

#include "FreeRTOS.h"
#include "semphr.h"

#include "lfs_util.h"
#include "lfs.h"
#include "lfs_port_prv.h"

int lfs_port_lock( const struct lfs_config * c )
{
    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) c->context;
    BaseType_t xReturnVal;

    xReturnVal = xSemaphoreTake( pxCtx->xMutex, pxCtx->xBlockTime );

    return ( int ) ( xReturnVal == pdTRUE ? 0 : -1 );
}

int lfs_port_unlock( const struct lfs_config * c )
{
    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) c->context;
    BaseType_t xReturnVal;

    xReturnVal = xSemaphoreGive( pxCtx->xMutex );

    return ( int ) ( xReturnVal == pdTRUE ? 0 : -1 );
}

/* The following function lfs_crc is derived from lfs_util.c and
 * is available under the following terms:
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
uint32_t lfs_crc( uint32_t crc,
                  const void * buffer,
                  size_t size )
{
    static const uint32_t rtable[ 16 ] =
    {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };

    const uint8_t * data = buffer;

    for( size_t i = 0; i < size; i++ )
    {
        crc = ( crc >> 4 ) ^ rtable[ ( crc ^ ( data[ i ] >> 0 ) ) & 0xf ];
        crc = ( crc >> 4 ) ^ rtable[ ( crc ^ ( data[ i ] >> 4 ) ) & 0xf ];
    }

    return crc;
}
