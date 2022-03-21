/*
 * FreeRTOS STM32 Reference Integration
 *
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved
 * SPDX-License-Identifier: BSD-3-Clause AND MIT
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

/*
 * Derived from lfs_util.h and inheriting the following terms:
 *
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause AND MIT
 */

#ifndef FS_LFS_CONFIG_H_
#define FS_LFS_CONFIG_H_

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C"
{
#endif
/* *INDENT-ON* */

#include "logging_levels.h"

#ifndef LOG_LEVEL
#define LOG_LEVEL    LOG_ERROR
#endif


#include "logging.h"

/* System includes */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include "FreeRTOS.h"

/* Configuration */
#define LFS_THREADSAFE
/* #define LFS_NO_MALLOC */

/* Logging */
#define LFS_TRACE( ... )    LogDebug( __VA_ARGS__ )
#define LFS_DEBUG( ... )    LogDebug( __VA_ARGS__ )
#define LFS_WARN( ... )     LogWarn( __VA_ARGS__ )
#define LFS_ERROR( ... )    LogError( __VA_ARGS__ )

/* Runtime assertions */
#define LFS_ASSERT( a )     configASSERT( a )


/* Builtin functions, these may be replaced by more efficient
 * toolchain-specific implementations. LFS_NO_INTRINSICS falls back to a more
 * expensive basic C implementation for debugging purposes */

/* Min/max functions for unsigned 32-bit numbers */
static inline uint32_t lfs_max( uint32_t a,
                                uint32_t b )
{
    return ( a > b ) ? a : b;
}

static inline uint32_t lfs_min( uint32_t a,
                                uint32_t b )
{
    return ( a < b ) ? a : b;
}

/* Align to nearest multiple of a size */
static inline uint32_t lfs_aligndown( uint32_t a,
                                      uint32_t alignment )
{
    return a - ( a % alignment );
}

static inline uint32_t lfs_alignup( uint32_t a,
                                    uint32_t alignment )
{
    return lfs_aligndown( a + alignment - 1, alignment );
}

/* Find the smallest power of 2 greater than or equal to a */
static inline uint32_t lfs_npw2( uint32_t a )
{
#if !defined( LFS_NO_INTRINSICS ) && ( defined( __GNUC__ ) || defined( __CC_ARM ) )
    return 32 - __builtin_clz( a - 1 );
#else
    uint32_t r = 0;
    uint32_t s;
    a -= 1;
    s = ( a > 0xffff ) << 4;
    a >>= s;
    r |= s;
    s = ( a > 0xff ) << 3;
    a >>= s;
    r |= s;
    s = ( a > 0xf ) << 2;
    a >>= s;
    r |= s;
    s = ( a > 0x3 ) << 1;
    a >>= s;
    r |= s;
    return ( r | ( a >> 1 ) ) + 1;
#endif /* if !defined( LFS_NO_INTRINSICS ) && ( defined( __GNUC__ ) || defined( __CC_ARM ) ) */
}

/* Count the number of trailing binary zeros in a */
/* lfs_ctz(0) may be undefined */
static inline uint32_t lfs_ctz( uint32_t a )
{
#if !defined( LFS_NO_INTRINSICS ) && defined( __GNUC__ )
    return __builtin_ctz( a );
#else
    return lfs_npw2( ( a & -a ) + 1 ) - 1;
#endif
}

/* Count the number of binary ones in a */
static inline uint32_t lfs_popc( uint32_t a )
{
#if !defined( LFS_NO_INTRINSICS ) && ( defined( __GNUC__ ) || defined( __CC_ARM ) )
    return __builtin_popcount( a );
#else
    a = a - ( ( a >> 1 ) & 0x55555555 );
    a = ( a & 0x33333333 ) + ( ( a >> 2 ) & 0x33333333 );
    return ( ( ( a + ( a >> 4 ) ) & 0xf0f0f0f ) * 0x1010101 ) >> 24;
#endif
}

/* Find the sequence comparison of a and b, this is the distance */
/* between a and b ignoring overflow */
static inline int lfs_scmp( uint32_t a,
                            uint32_t b )
{
    return ( int ) ( unsigned ) ( a - b );
}

/* Convert between 32-bit little-endian and native order */
static inline uint32_t lfs_fromle32( uint32_t a )
{
#if !defined( LFS_NO_INTRINSICS ) && (                                                                          \
        ( defined( BYTE_ORDER ) && defined( ORDER_LITTLE_ENDIAN ) && BYTE_ORDER == ORDER_LITTLE_ENDIAN ) ||     \
    ( defined( __BYTE_ORDER ) && defined( __ORDER_LITTLE_ENDIAN ) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN ) || \
    ( defined( __BYTE_ORDER__ ) && defined( __ORDER_LITTLE_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ) )
    return a;
#elif !defined( LFS_NO_INTRINSICS ) && (                                                                  \
        ( defined( BYTE_ORDER ) && defined( ORDER_BIG_ENDIAN ) && BYTE_ORDER == ORDER_BIG_ENDIAN ) ||     \
    ( defined( __BYTE_ORDER ) && defined( __ORDER_BIG_ENDIAN ) && __BYTE_ORDER == __ORDER_BIG_ENDIAN ) || \
    ( defined( __BYTE_ORDER__ ) && defined( __ORDER_BIG_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ) )
    return __builtin_bswap32( a );
#else
    return ( ( ( uint8_t * ) &a )[ 0 ] << 0 ) |
           ( ( ( uint8_t * ) &a )[ 1 ] << 8 ) |
           ( ( ( uint8_t * ) &a )[ 2 ] << 16 ) |
           ( ( ( uint8_t * ) &a )[ 3 ] << 24 );
#endif /* if !defined( LFS_NO_INTRINSICS ) && ( ( defined( BYTE_ORDER ) && defined( ORDER_LITTLE_ENDIAN ) && BYTE_ORDER == ORDER_LITTLE_ENDIAN ) || ( defined( __BYTE_ORDER ) && defined( __ORDER_LITTLE_ENDIAN ) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN ) || ( defined( __BYTE_ORDER__ ) && defined( __ORDER_LITTLE_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ) ) */
}

static inline uint32_t lfs_tole32( uint32_t a )
{
    return lfs_fromle32( a );
}

/* Convert between 32-bit big-endian and native order */
static inline uint32_t lfs_frombe32( uint32_t a )
{
#if !defined( LFS_NO_INTRINSICS ) && (                                                                          \
        ( defined( BYTE_ORDER ) && defined( ORDER_LITTLE_ENDIAN ) && BYTE_ORDER == ORDER_LITTLE_ENDIAN ) ||     \
    ( defined( __BYTE_ORDER ) && defined( __ORDER_LITTLE_ENDIAN ) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN ) || \
    ( defined( __BYTE_ORDER__ ) && defined( __ORDER_LITTLE_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ) )
    return __builtin_bswap32( a );
#elif !defined( LFS_NO_INTRINSICS ) && (                                                                  \
        ( defined( BYTE_ORDER ) && defined( ORDER_BIG_ENDIAN ) && BYTE_ORDER == ORDER_BIG_ENDIAN ) ||     \
    ( defined( __BYTE_ORDER ) && defined( __ORDER_BIG_ENDIAN ) && __BYTE_ORDER == __ORDER_BIG_ENDIAN ) || \
    ( defined( __BYTE_ORDER__ ) && defined( __ORDER_BIG_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ) )
    return a;
#else
    return ( ( ( uint8_t * ) &a )[ 0 ] << 24 ) |
           ( ( ( uint8_t * ) &a )[ 1 ] << 16 ) |
           ( ( ( uint8_t * ) &a )[ 2 ] << 8 ) |
           ( ( ( uint8_t * ) &a )[ 3 ] << 0 );
#endif /* if !defined( LFS_NO_INTRINSICS ) && ( ( defined( BYTE_ORDER ) && defined( ORDER_LITTLE_ENDIAN ) && BYTE_ORDER == ORDER_LITTLE_ENDIAN ) || ( defined( __BYTE_ORDER ) && defined( __ORDER_LITTLE_ENDIAN ) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN ) || ( defined( __BYTE_ORDER__ ) && defined( __ORDER_LITTLE_ENDIAN__ ) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ) ) */
}

static inline uint32_t lfs_tobe32( uint32_t a )
{
    return lfs_frombe32( a );
}

/* Calculate CRC-32 with polynomial = 0x04c11db7 */
uint32_t lfs_crc( uint32_t crc,
                  const void * buffer,
                  size_t size );

#if !defined( LFS_NO_MALLOC ) && !defined( configSUPPORT_DYNAMIC_ALLOCATION )
#error "You must define either LFS_NO_MALLOC or configSUPPORT_DYNAMIC_ALLOCATION"
#endif

static inline void * lfs_malloc( size_t size )
{
#ifndef LFS_NO_MALLOC
    return pvPortMalloc( size );
#else
    ( void ) size;
    return NULL;
#endif
}

static inline void lfs_free( void * p )
{
#ifndef LFS_NO_MALLOC
    vPortFree( p );
#else
    ( void ) p;
#endif
}

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* FS_LFS_CONFIG_H_ */
