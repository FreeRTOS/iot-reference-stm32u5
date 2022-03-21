/*
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
#include "kvstore.h"
#include "kvstore_prv.h"
#include <string.h>

static SemaphoreHandle_t xKvMutex = NULL;

#if KV_STORE_CACHE_ENABLE
#define READ_ENTRY     xprvCopyValueFromCache
#define WRITE_ENTRY    xprvWriteCacheEntry
#else
#define READ_ENTRY     xprvReadValueFromImplStatic
#define WRITE_ENTRY    xprvWriteValueToImpl
#endif

const char * const kvStoreKeyMap[ CS_NUM_KEYS ] = KV_STORE_STRINGS;

const KVStoreDefaultEntry_t kvStoreDefaults[ CS_NUM_KEYS ] = KV_STORE_DEFAULTS;

static size_t xReadEntryOrDefault( KVStoreKey_t xKey,
                                   void * pvBuffer,
                                   size_t xBufferSize )
{
    size_t xLength = 0;

    configASSERT( xKey < CS_NUM_KEYS );
    configASSERT( pvBuffer != NULL );

    ( void ) READ_ENTRY( xKey, NULL, &xLength, pvBuffer, xBufferSize );

    if( xLength == 0 )
    {
        size_t xDataLen = kvStoreDefaults[ xKey ].length;

        if( xBufferSize < xDataLen )
        {
            LogWarn( "Read from key: %s was truncated from %d bytes to %d bytes.",
                     kvStoreKeyMap[ xKey ], xDataLen, xBufferSize );
            xDataLen = xBufferSize;
        }

        if( xDataLen > sizeof( void * ) )
        {
            ( void ) memcpy( pvBuffer, kvStoreDefaults[ xKey ].blob, xDataLen );
        }
        else
        {
            ( void ) memcpy( pvBuffer, &( kvStoreDefaults[ xKey ].u32 ), xDataLen );
        }

        xLength = kvStoreDefaults[ xKey ].length;
    }

    return xLength;
}

/*
 * @brief Initialize KeyValue store and load runtime configuration from flash into ram.
 * Must be called after filesystem has been initialized.
 */
void KVStore_init( void )
{
    if( xKvMutex == NULL )
    {
        xKvMutex = xSemaphoreCreateMutex();
    }

    ( void ) xSemaphoreTake( xKvMutex, portMAX_DELAY );

#if KV_STORE_CACHE_ENABLE
    vprvCacheInit();
#endif

#if KV_STORE_NVIMPL_ENABLE
    vprvNvImplInit();
#endif

    ( void ) xSemaphoreGive( xKvMutex );
}

BaseType_t KVStore_setBlob( KVStoreKey_t key,
                            size_t xLength,
                            const void * pvNewValue )
{
    BaseType_t xReturn = pdFALSE;

    if( ( key < CS_NUM_KEYS ) && ( pvNewValue != NULL ) && ( xLength > 0 ) &&
        ( kvStoreDefaults[ key ].type == KV_TYPE_BLOB ) )
    {
        xReturn = WRITE_ENTRY( key, KV_TYPE_BLOB, xLength, pvNewValue );
    }

    return xReturn;
}

BaseType_t KVStore_setString( KVStoreKey_t key,
                              const char * pcNewValue )
{
    BaseType_t xReturn = pdFALSE;

    if( ( key < CS_NUM_KEYS ) &&
        ( pcNewValue != NULL ) &&
        ( kvStoreDefaults[ key ].type == KV_TYPE_STRING ) )
    {
        xReturn = WRITE_ENTRY( key, KV_TYPE_STRING, strlen( pcNewValue ) + 1, ( const void * ) pcNewValue );
    }

    return xReturn;
}

BaseType_t KVStore_setUInt32( KVStoreKey_t key,
                              uint32_t ulNewVal )
{
    BaseType_t xReturn = pdFALSE;

    if( ( key < CS_NUM_KEYS ) && ( kvStoreDefaults[ key ].type == KV_TYPE_UINT32 ) )
    {
        xReturn = WRITE_ENTRY( key, KV_TYPE_UINT32, sizeof( uint32_t ), ( const void * ) &ulNewVal );
    }

    return xReturn;
}

BaseType_t KVStore_setInt32( KVStoreKey_t key,
                             int32_t lNewVal )
{
    BaseType_t xReturn = pdFALSE;

    if( ( key < CS_NUM_KEYS ) && ( kvStoreDefaults[ key ].type == KV_TYPE_INT32 ) )
    {
        xReturn = WRITE_ENTRY( key, KV_TYPE_INT32, sizeof( int32_t ), ( const void * ) &lNewVal );
    }

    return xReturn;
}

BaseType_t KVStore_setUBase( KVStoreKey_t key,
                             UBaseType_t uxNewVal )
{
    BaseType_t xReturn = pdFALSE;

    if( ( key < CS_NUM_KEYS ) && ( kvStoreDefaults[ key ].type == KV_TYPE_UBASE_T ) )
    {
        xReturn = WRITE_ENTRY( key, KV_TYPE_UBASE_T, sizeof( UBaseType_t ),
                               ( const void * ) &uxNewVal );
    }

    return xReturn;
}

BaseType_t KVStore_setBase( KVStoreKey_t key,
                            BaseType_t xNewVal )
{
    BaseType_t xReturn = pdFALSE;

    if( ( key < CS_NUM_KEYS ) && ( kvStoreDefaults[ key ].type == KV_TYPE_BASE_T ) )
    {
        xReturn = WRITE_ENTRY( key, KV_TYPE_BASE_T, sizeof( BaseType_t ), ( const void * ) &xNewVal );
    }

    return xReturn;
}

size_t KVStore_getSize( KVStoreKey_t xKey )
{
    size_t xDataLen = 0;

    if( xKey < CS_NUM_KEYS )
    {
        /* First check cache if available */
#if KV_STORE_CACHE_ENABLE
        xDataLen = prvGetCacheEntryLength( xKey );
#else
        /* otherwise read directly from NV */
        xDataLen = xprvGetValueLengthFromImpl( xKey );
#endif

        if( xDataLen == 0 )
        {
            /* Otherwise read default value */
            xDataLen = kvStoreDefaults[ xKey ].length;
        }
    }

    return xDataLen;
}

size_t KVStore_getBlob( KVStoreKey_t key,
                        void * pvBuffer,
                        size_t xMaxLength )
{
    size_t xLength = 0;

    if( ( key < CS_NUM_KEYS ) && ( pvBuffer != NULL ) && ( kvStoreDefaults[ key ].type == KV_TYPE_BLOB ) )
    {
        ( void ) xSemaphoreTake( xKvMutex, portMAX_DELAY );

        xLength = xReadEntryOrDefault( key, pvBuffer, xMaxLength );

        ( void ) xSemaphoreGive( xKvMutex );
    }

    return xLength;
}

void * KVStore_getBlobHeap( KVStoreKey_t key,
                            size_t * pxLength )
{
    size_t xLen = KVStore_getSize( key );
    void * pvBuffer = NULL;

    if( xLen > 0 )
    {
        pvBuffer = pvPortMalloc( xLen );

        if( pvBuffer != NULL )
        {
            if( KVStore_getBlob( key, pvBuffer, xLen ) == 0 )
            {
                vPortFree( pvBuffer );
                pvBuffer = NULL;
                xLen = 0;
                configASSERT_CONTINUE( 0 );
            }
        }
        else
        {
            LogError( "Failed to allocate %ld bytes.", xLen );
        }
    }

    if( pxLength != NULL )
    {
        *pxLength = xLen;
    }

    return pvBuffer;
}

KVStoreValueType_t KVStore_getType( KVStoreKey_t key )
{
    KVStoreValueType_t xKvType = KV_TYPE_NONE;

    if( key < CS_NUM_KEYS )
    {
        xKvType = kvStoreDefaults[ key ].type;
    }

    return( xKvType );
}

size_t KVStore_getString( KVStoreKey_t key,
                          char * pcBuffer,
                          size_t xMaxLength )
{
    size_t xSizeWritten = 0;

    if( ( key < CS_NUM_KEYS ) &&
        ( pcBuffer != NULL ) &&
        ( kvStoreDefaults[ key ].type == KV_TYPE_STRING ) )
    {
        ( void ) xSemaphoreTake( xKvMutex, portMAX_DELAY );

        xSizeWritten = xReadEntryOrDefault( key, ( void * ) pcBuffer, xMaxLength );

        /* Ensure null terminated */
        pcBuffer[ xMaxLength - 1 ] = '\0';

        ( void ) xSemaphoreGive( xKvMutex );
    }

    /* Remove null terminator from returned count */
    if( xSizeWritten > 0 )
    {
        xSizeWritten = xSizeWritten - 1;
    }

    return xSizeWritten;
}

char * KVStore_getStringHeap( KVStoreKey_t key,
                              size_t * pxLength )
{
    size_t xLen = KVStore_getSize( key );
    char * pcBuffer = NULL;

    if( xLen > 0 )
    {
        pcBuffer = pvPortMalloc( xLen );

        if( pcBuffer != NULL )
        {
            if( KVStore_getString( key, pcBuffer, xLen ) == 0 )
            {
                vPortFree( pcBuffer );
                pcBuffer = NULL;
                xLen = 0;
                configASSERT_CONTINUE( 0 );
            }
        }
        else
        {
            LogError( "Failed to allocate %ld bytes.", xLen );
        }
    }

    if( pxLength != NULL )
    {
        *pxLength = xLen - 1;
    }

    return pcBuffer;
}

uint32_t KVStore_getUInt32( KVStoreKey_t key,
                            BaseType_t * pxSuccess )
{
    uint32_t ulReturnValue = 0;

    size_t xSizeWritten = 0;

    if( ( key < CS_NUM_KEYS ) &&
        ( kvStoreDefaults[ key ].type == KV_TYPE_UINT32 ) )
    {
        ( void ) xSemaphoreTake( xKvMutex, portMAX_DELAY );

        xSizeWritten = xReadEntryOrDefault( key, ( void * ) &ulReturnValue,
                                            sizeof( uint32_t ) );

        ( void ) xSemaphoreGive( xKvMutex );
    }

    if( pxSuccess != NULL )
    {
        *pxSuccess = ( xSizeWritten == sizeof( uint32_t ) );
    }

    return ulReturnValue;
}

int32_t KVStore_getInt32( KVStoreKey_t key,
                          BaseType_t * pxSuccess )
{
    int32_t lReturnValue = 0;

    size_t xSizeWritten = 0;

    if( ( key < CS_NUM_KEYS ) && ( kvStoreDefaults[ key ].type == KV_TYPE_INT32 ) )
    {
        ( void ) xSemaphoreTake( xKvMutex, portMAX_DELAY );

        xSizeWritten = xReadEntryOrDefault( key, ( void * ) &lReturnValue, sizeof( int32_t ) );

        ( void ) xSemaphoreGive( xKvMutex );
    }

    if( pxSuccess != NULL )
    {
        *pxSuccess = ( xSizeWritten == sizeof( int32_t ) );
    }

    return lReturnValue;
}

BaseType_t KVStore_getBase( KVStoreKey_t key,
                            BaseType_t * pxSuccess )
{
    BaseType_t xReturnValue = 0;

    size_t xSizeWritten = 0;

    if( ( key < CS_NUM_KEYS ) && ( kvStoreDefaults[ key ].type == KV_TYPE_BASE_T ) )
    {
        ( void ) xSemaphoreTake( xKvMutex, portMAX_DELAY );

        xSizeWritten = xReadEntryOrDefault( key, ( void * ) &xReturnValue, sizeof( BaseType_t ) );

        ( void ) xSemaphoreGive( xKvMutex );
    }

    if( pxSuccess != NULL )
    {
        *pxSuccess = ( xSizeWritten == sizeof( BaseType_t ) );
    }

    return xReturnValue;
}

UBaseType_t KVStore_getUBase( KVStoreKey_t key,
                              BaseType_t * pxSuccess )
{
    UBaseType_t xReturnValue = 0;

    size_t xSizeWritten = 0;

    if( ( key < CS_NUM_KEYS ) && ( kvStoreDefaults[ key ].type == KV_TYPE_BASE_T ) )
    {
        ( void ) xSemaphoreTake( xKvMutex, portMAX_DELAY );

        xSizeWritten = xReadEntryOrDefault( key, ( void * ) &xReturnValue, sizeof( UBaseType_t ) );

        ( void ) xSemaphoreGive( xKvMutex );
    }

    if( pxSuccess != NULL )
    {
        *pxSuccess = ( xSizeWritten == sizeof( UBaseType_t ) );
    }

    return xReturnValue;
}

const char * kvKeyToString( KVStoreKey_t xKey )
{
    const char * retVal = NULL;

    if( ( xKey < CS_NUM_KEYS ) && ( xKey >= 0 ) )
    {
        retVal = kvStoreKeyMap[ xKey ];
    }

    return retVal;
}

KVStoreKey_t kvStringToKey( const char * pcKey )
{
    KVStoreKey_t xKey = CS_NUM_KEYS;

    for( uint32_t i = 0; i < CS_NUM_KEYS; i++ )
    {
        if( 0 == strcmp( kvStoreKeyMap[ i ], pcKey ) )
        {
            xKey = i;
            break;
        }
    }

    return xKey;
}
