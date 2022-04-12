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
#include "kvstore_prv.h"
#include <string.h>

#if KV_STORE_CACHE_ENABLE

typedef struct
{
    KVStoreValueType_t type;
    size_t length;
    union
    {
        void * pvData;
        char * pcData;
        UBaseType_t uxData;
        BaseType_t xData;
        uint32_t ulData;
        int32_t lData;
    };
    BaseType_t xChangePending;
} KVStoreCacheEntry_t;

static KVStoreCacheEntry_t kvStoreCache[ CS_NUM_KEYS ] = { 0 };


static inline void * pvGetDataWritePtr( KVStoreKey_t key )
{
    void * pvData = NULL;

    if( kvStoreCache[ key ].length > sizeof( void * ) )
    {
        pvData = kvStoreCache[ key ].pvData;
    }
    else
    {
        pvData = ( void * ) &( kvStoreCache[ key ].pvData );
    }

    configASSERT( pvData != NULL );
    return pvData;
}

static inline const void * pvGetDataReadPtr( KVStoreKey_t key )
{
    const void * pvData = NULL;

    if( kvStoreCache[ key ].type == KV_TYPE_NONE )
    {
        pvData = NULL;
    }
    else if( kvStoreCache[ key ].length > sizeof( void * ) )
    {
        pvData = kvStoreCache[ key ].pvData;
    }
    else
    {
        pvData = ( void * ) &( kvStoreCache[ key ].pvData );
    }

    return pvData;
}

static inline void vAllocateDataBuffer( KVStoreKey_t key,
                                        size_t xNewLength )
{
    if( xNewLength > sizeof( void * ) )
    {
        kvStoreCache[ key ].pvData = pvPortMalloc( xNewLength );
        kvStoreCache[ key ].length = xNewLength;
    }
    else
    {
        kvStoreCache[ key ].ulData = 0;
        kvStoreCache[ key ].length = xNewLength;
    }
}

static inline void vClearDataBuffer( KVStoreKey_t key )
{
    /* Check if data is heap allocated > sizeof( void * ) */
    if( kvStoreCache[ key ].length > sizeof( void * ) )
    {
        vPortFree( kvStoreCache[ key ].pvData );
        kvStoreCache[ key ].pvData = NULL;
        kvStoreCache[ key ].length = 0;
    }
    else /* Statically allocated */
    {
        kvStoreCache[ key ].length = 0;
        kvStoreCache[ key ].xData = 0;
    }
}

static inline void vReallocDataBuffer( KVStoreKey_t key,
                                       size_t xNewLength )
{
    if( xNewLength > kvStoreCache[ key ].length )
    {
        /* Need to allocate a bigger buffer */
        vClearDataBuffer( key );
        vAllocateDataBuffer( key, xNewLength );
    }
    else /* New value is same size or smaller. Re-use already allocated buffer */
    {
        kvStoreCache[ key ].length = xNewLength;
    }
}

/*
 * @brief Initialize the Key Value Store Cache by reading each entry from the storage nvm store.
 */
void vprvCacheInit( void )
{
#if KV_STORE_NVIMPL_ENABLE
    /* Read from file system into ram */
    for( uint32_t i = 0; i < CS_NUM_KEYS; i++ )
    {
        /* pvData pointer should be NULL on startup */
        configASSERT_CONTINUE( kvStoreCache[ i ].pvData == NULL );


        kvStoreCache[ i ].xChangePending = pdFALSE;
        kvStoreCache[ i ].type = KV_TYPE_NONE;

        size_t xNvLength = xprvGetValueLengthFromImpl( i );

        if( xNvLength > 0 )
        {
            vAllocateDataBuffer( i, xNvLength );

            KVStoreValueType_t * pxType = &( kvStoreCache[ i ].type );
            size_t * pxLength = &( kvStoreCache[ i ].length );

            ( void ) xprvReadValueFromImpl( i, pxType, pxLength, pvGetDataWritePtr( i ), *pxLength );
        }
    }
#endif /* KV_STORE_NVIMPL_ENABLE */
}

/*
 * @brief Get the length of the value stored in the cache corresponding to a given key.
 * @param[in] xKey The key to lookup.
 * @return the length of the entry or 0 if non-existent.
 */
size_t prvGetCacheEntryLength( KVStoreKey_t xKey )
{
    configASSERT( xKey < CS_NUM_KEYS );
    return kvStoreCache[ xKey ].length;
}

/*
 * @brief Get the type of the value stored in the cache corresponding to a given key.
 * @param[in] xKey The key to lookup.
 * @return the type of the entry or KV_TYPE_NONE if non-existent.
 */
KVStoreValueType_t prvGetCacheEntryType( KVStoreKey_t xKey )
{
    configASSERT( xKey < CS_NUM_KEYS );
    return kvStoreCache[ xKey ].type;
}

/*
 * @brief Write a given and / value pair to the cache
 * @param[in] xKey Key to store the provided value in
 * @param[in] xNewType The type of the data to store.
 * @param[in] xLength Length of the data to store.
 * @param[in] pvNewValue Pointer to the new data to be copied into the cache.
 * @return pdTRUE always.
 */
BaseType_t xprvWriteCacheEntry( KVStoreKey_t xKey,
                                KVStoreValueType_t xNewType,
                                size_t xLength,
                                const void * pvNewValue )
{
    configASSERT( xKey < CS_NUM_KEYS );
    configASSERT( xNewType < KV_TYPE_LAST );
    configASSERT( xLength > 0 );
    configASSERT( pvNewValue != NULL );

    /* Check if value is not currently set */
    if( kvStoreCache[ xKey ].type == KV_TYPE_NONE )
    {
        vAllocateDataBuffer( xKey, xLength );
        kvStoreCache[ xKey ].type = xNewType;
        kvStoreCache[ xKey ].xChangePending = pdTRUE;
    }
    /* Check for change in length */
    else if( kvStoreCache[ xKey ].length != xLength )
    {
        vReallocDataBuffer( xKey, xLength );
        kvStoreCache[ xKey ].type = xNewType;
        kvStoreCache[ xKey ].xChangePending = pdTRUE;
    }
    /* Check for change in type */
    else if( kvStoreCache[ xKey ].type != xNewType )
    {
        kvStoreCache[ xKey ].type = xNewType;
        kvStoreCache[ xKey ].xChangePending = pdTRUE;
    }
    /* Otherwise, type / length are the same, so check value */
    else
    {
        const void * pvReadPtr = pvGetDataReadPtr( xKey );

        if( ( pvReadPtr == NULL ) ||
            ( memcmp( pvReadPtr, pvNewValue, xLength ) != 0 ) )
        {
            kvStoreCache[ xKey ].xChangePending = pdTRUE;
        }
    }

    if( kvStoreCache[ xKey ].xChangePending == pdTRUE )
    {
        void * pvDataWrite = pvGetDataWritePtr( xKey );

        if( pvDataWrite != NULL )
        {
            ( void ) memcpy( pvGetDataWritePtr( xKey ), pvNewValue, xLength );
        }
    }

    return pdTRUE;
}


BaseType_t xprvCopyValueFromCache( KVStoreKey_t xKey,
                                   KVStoreValueType_t * pxDataType,
                                   size_t * pxDataLength,
                                   void * pvBuffer,
                                   size_t xBufferSize )
{
    const void * pvDataPtr = NULL;
    size_t xDataLen = 0;

    configASSERT( xKey < CS_NUM_KEYS );
    configASSERT( pvBuffer != NULL );

    pvDataPtr = pvGetDataReadPtr( xKey );

    if( pvDataPtr != NULL )
    {
        xDataLen = kvStoreCache[ xKey ].length;

        if( xBufferSize < xDataLen )
        {
            LogWarn( "Read from key: %s was truncated from %d bytes to %d bytes.",
                     kvStoreKeyMap[ xKey ], xDataLen, xBufferSize );
            xDataLen = xBufferSize;
        }

        ( void ) memcpy( pvBuffer, pvDataPtr, xDataLen );

        if( pxDataType != NULL )
        {
            *pxDataType = kvStoreCache[ xKey ].type;
        }

        if( pxDataLength != NULL )
        {
            *pxDataLength = kvStoreCache[ xKey ].length;
        }
    }

    return( xDataLen > 0 );
}

BaseType_t KVStore_xCommitChanges( void )
{
    BaseType_t xSuccess = pdTRUE;

#if KV_STORE_NVIMPL_ENABLE
    for( uint32_t i = 0; i < CS_NUM_KEYS; i++ )
    {
        if( kvStoreCache[ i ].xChangePending == pdTRUE )
        {
            xSuccess &= xprvWriteValueToImpl( i,
                                              kvStoreCache[ i ].type,
                                              kvStoreCache[ i ].length,
                                              pvGetDataReadPtr( i ) );
        }
    }
#endif /* if KV_STORE_NVIMPL_ENABLE */
    return xSuccess;
}

#endif /* KV_STORE_CACHE_ENABLE */
