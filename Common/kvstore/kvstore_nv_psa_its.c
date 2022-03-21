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


#include "logging_levels.h"
#include "logging.h"
#include "kvstore_prv.h"
#include <string.h>

#if KV_STORE_NVIMPL_ARM_PSA
#include "psa/internal_trusted_storage.h"

#define KVSTORE_UID_OFFSET    0x1234

typedef struct
{
    KVStoreValueType_t type;
    size_t length; /* Length of value portion (excludes type and length fields */
} KVStoreHeader_t;

static inline psa_storage_uid_t xKeyToUID( KVStoreKey_t xKey )
{
    return( KVSTORE_UID_OFFSET + xKey );
}

static inline BaseType_t xPSAStatusToBool( psa_status_t xStatus )
{
    return( xStatus == PSA_SUCCESS ? pdTRUE : pdFALSE );
}

/*
 * @brief Get the length of a value stored in the KVStore implementation
 * @param[in] xKey Key to lookup
 * @return length of the value stored in the KVStore or 0 if not found.
 */
size_t xprvGetValueLengthFromImpl( const KVStoreKey_t xKey )
{
    size_t xLength = 0;
    struct psa_storage_info_t xStorageInfo = { 0 };

    if( psa_its_get_info( xKeyToUID( xKey ), &xStorageInfo ) == PSA_SUCCESS )
    {
        xLength = xStorageInfo.size - sizeof( KVStoreHeader_t );
    }

    return xLength;
}

/*
 * @brief Read the value for the given key into a given buffer.
 * @param[in] xKey The key to lookup
 * @param[out] pxType The type of the value returned.
 * @param[out] pxLength Pointer to store the length of the read value in.
 * @param[out] pvBuffer The buffer to copy the value to.
 * @param[in] xBufferSize The length of the provided buffer.
 * @return pdTRUE on success, otherwise pdFALSE.
 */
BaseType_t xprvReadValueFromImpl( const KVStoreKey_t xKey,
                                  KVStoreValueType_t * pxType,
                                  size_t * pxLength,
                                  void * pvBuffer,
                                  size_t xBufferSize )
{
    size_t uxDataLength = 0;
    psa_status_t xResult = PSA_SUCCESS;
    KVStoreHeader_t xHeader = { 0 };

    xHeader.length = 0;
    xHeader.type = KV_TYPE_NONE;

    if( ( pvBuffer == NULL ) || ( xBufferSize == 0 ) )
    {
        xResult = -1;
    }

    /* Read header */
    if( xResult == PSA_SUCCESS )
    {
        xResult = psa_its_get( xKeyToUID( xKey ),
                               0,                         /* Offset */
                               sizeof( KVStoreHeader_t ), /* read length */
                               &xHeader,                  /* buffer address */
                               &uxDataLength );

        if( uxDataLength != sizeof( KVStoreHeader_t ) )
        {
            xResult = -1;
            uxDataLength = 0;
        }
    }

    if( xResult == PSA_SUCCESS )
    {
        if( xBufferSize < xHeader.length )
        {
            xResult = psa_its_get( xKeyToUID( xKey ),
                                   sizeof( KVStoreHeader_t ),
                                   xBufferSize,
                                   pvBuffer,
                                   &uxDataLength );

            configASSERT( uxDataLength == xBufferSize );
        }
        else
        {
            xResult = psa_its_get( xKeyToUID( xKey ),
                                   sizeof( KVStoreHeader_t ),
                                   xHeader.length,
                                   pvBuffer,
                                   &uxDataLength );
            configASSERT( uxDataLength == xHeader.length );
        }
    }

    /* Set type if input is not null */
    if( pxType != NULL )
    {
        *pxType = xHeader.type;
    }

    /* Set length if input is not null */
    if( pxLength != NULL )
    {
        *pxLength = uxDataLength;
    }

    return xPSAStatusToBool( xResult );
}

/*
 * @brief Write a value for a given key to non-volatile storage.
 * @param[in] xKey Key to store the given value in.
 * @param[in] xType Type of value to record.
 * @param[in] xLength length of the value given in pxDataUnion.
 * @param[in] pxData Pointer to a buffer containing the value to be stored.
 * The caller must free any heap allocated buffers passed into this function.
 */
BaseType_t xprvWriteValueToImpl( const KVStoreKey_t xKey,
                                 const KVStoreValueType_t xType,
                                 const size_t xLength,
                                 const void * pvData )
{
    psa_status_t xResult = PSA_SUCCESS;
    void * pvBuffer = NULL;

    if( ( xKey > CS_NUM_KEYS ) ||
        ( xType == KV_TYPE_NONE ) ||
        ( xLength < 0 ) ||
        ( pvData == NULL ) )
    {
        xResult = -1;
    }

    /* Stage in memory to reduce number of flash writes required */
    if( xResult == PSA_SUCCESS )
    {
        pvBuffer = pvPortMalloc( sizeof( KVStoreHeader_t ) + xLength );

        if( pvBuffer == NULL )
        {
            configASSERT_CONTINUE( pvBuffer != NULL );
            xResult = -1;
        }
    }

    if( xResult == PSA_SUCCESS )
    {
        KVStoreHeader_t * pxHeader = ( KVStoreHeader_t * ) pvBuffer;

        pxHeader->length = xLength;
        pxHeader->type = xType;

        ( void ) memcpy( pvBuffer + sizeof( KVStoreHeader_t ), pvData, xLength );

        xResult = psa_its_set( xKeyToUID( xKey ),
                               sizeof( KVStoreHeader_t ) + xLength,
                               pvBuffer,
                               0 );
    }

    /*
     * Clear any sensitive data stored in ram temporarily
     * Free heap allocated buffer
     */
    if( pvBuffer != NULL )
    {
        explicit_bzero( pvBuffer, sizeof( KVStoreHeader_t ) + xLength );

        vPortFree( pvBuffer );
        pvBuffer = NULL;
    }

    return xPSAStatusToBool( xResult );
}

void vprvNvImplInit( void )
{
/*	tfm_its_init(); */
}

#endif /* KV_STORE_NVIMPL_ARM_PSA */
