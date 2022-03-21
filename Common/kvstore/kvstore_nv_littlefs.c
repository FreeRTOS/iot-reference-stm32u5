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
#include "semphr.h"

#ifdef KV_STORE_NVIMPL_LITTLEFS
#include "lfs.h"
#include "fs/lfs_port.h"

#define KVSTORE_PREFIX        "/cfg/"
#define KVSTORE_MAX_FNANME    ( sizeof( KVSTORE_PREFIX ) + KVSTORE_KEY_MAX_LEN )

typedef struct
{
    KVStoreValueType_t type;
    size_t length; /* Length of value portion (excludes type and length fields */
} KVStoreTLVHeader_t;

static inline void vLfsSSizeToErr( lfs_ssize_t * pxReturnValue,
                                   size_t xExpectedLength )
{
    if( *pxReturnValue == xExpectedLength )
    {
        *pxReturnValue = LFS_ERR_OK;
    }
    else if( *pxReturnValue >= 0 )
    {
        *pxReturnValue = LFS_ERR_CORRUPT;
    }
    else
    {
        /* Pass through the error code otherwise */
    }
}

static inline BaseType_t xValidateFile( lfs_t * pLfsCtx,
                                        const char * pcFileName )
{
    BaseType_t xSuccess = pdFALSE;
    struct lfs_info xFileInfo = { 0 };

    if( ( lfs_stat( pLfsCtx, pcFileName, &xFileInfo ) == LFS_ERR_OK ) &&
        ( ( xFileInfo.type & LFS_TYPE_REG ) == LFS_TYPE_REG ) &&
        ( xFileInfo.size >= sizeof( KVStoreTLVHeader_t ) ) )
    {
        xSuccess = pdTRUE;
    }

    return xSuccess;
}

/*
 * @brief Get the length of a value stored in the KVStore implementation
 * @param[in] xKey Key to lookup
 * @return length of the value stored in the KVStore or 0 if not found.
 */
size_t xprvGetValueLengthFromImpl( KVStoreKey_t xKey )
{
    char pcFileName[ KVSTORE_MAX_FNANME ] = { 0 };
    lfs_t * pLfsCtx = pxGetDefaultFsCtx();
    struct lfs_info xFileInfo = { 0 };
    size_t xLength = 0;

    ( void ) strncpy( pcFileName, KVSTORE_PREFIX, KVSTORE_MAX_FNANME );
    ( void ) strncat( pcFileName, kvStoreKeyMap[ xKey ], KVSTORE_MAX_FNANME );

    if( lfs_stat( pLfsCtx, pcFileName, &xFileInfo ) == LFS_ERR_OK )
    {
        xLength = ( xFileInfo.size - sizeof( KVStoreTLVHeader_t ) );
    }

    return xLength;
}

BaseType_t xprvReadValueFromImpl( KVStoreKey_t xKey,
                                  KVStoreValueType_t * pxType,
                                  size_t * pxLength,
                                  void * pvBuffer,
                                  size_t xBufferSize )
{
    char pcFileName[ KVSTORE_MAX_FNANME ] = { 0 };

    lfs_t * pLfsCtx = pxGetDefaultFsCtx();
    lfs_ssize_t lReturn = LFS_ERR_CORRUPT;
    BaseType_t xFileOpenFlag = pdFALSE;

    ( void ) strncpy( pcFileName, KVSTORE_PREFIX, KVSTORE_MAX_FNANME );
    ( void ) strncat( pcFileName, kvStoreKeyMap[ xKey ], KVSTORE_MAX_FNANME );

    if( xValidateFile( pLfsCtx, pcFileName ) == pdTRUE )
    {
        lfs_file_t xFile = { 0 };
        KVStoreTLVHeader_t xTlvHeader = { 0 };

        /* Open the file */
        lReturn = lfs_file_open( pLfsCtx, &xFile, pcFileName, LFS_O_RDONLY );

        /* Read the header */
        if( lReturn == LFS_ERR_OK )
        {
            xFileOpenFlag = pdTRUE;
            lReturn = lfs_file_read( pLfsCtx, &xFile,
                                     &xTlvHeader, sizeof( KVStoreTLVHeader_t ) );

            vLfsSSizeToErr( &lReturn, sizeof( KVStoreTLVHeader_t ) );
        }

        configASSERT( ( xTlvHeader.length ) < KVSTORE_VAL_MAX_LEN );

        /* copy data to provided buffer */
        if( lReturn >= LFS_ERR_OK )
        {
            lReturn = lfs_file_read( pLfsCtx, &xFile,
                                     pvBuffer,
                                     xTlvHeader.length );
            vLfsSSizeToErr( &lReturn, xTlvHeader.length );
        }

        if( lReturn == LFS_ERR_OK )
        {
            if( pxType != NULL )
            {
                if( lReturn == LFS_ERR_OK )
                {
                    *pxType = xTlvHeader.type;
                }
                else
                {
                    *pxType = KV_TYPE_NONE;
                }
            }

            if( pxLength != NULL )
            {
                if( lReturn == LFS_ERR_OK )
                {
                    *pxLength = xTlvHeader.length;
                }
                else
                {
                    *pxLength = 0;
                }
            }
        }

        if( xFileOpenFlag == pdTRUE )
        {
            ( void ) lfs_file_close( pLfsCtx, &xFile );
        }
    }

    return( lReturn == LFS_ERR_OK );
}

/*
 * @brief Write a value for a given key to non-volatile storage.
 * @param[in] xKey Key to store the given value in.
 * @param[in] xType Type of value to record.
 * @param[in] xLength length of the value given in pxDataUnion.
 * @param[in] pxData Pointer to a buffer containing the value to be stored.
 * The caller must free any heap allocated buffers passed into this function.
 */
BaseType_t xprvWriteValueToImpl( KVStoreKey_t xKey,
                                 KVStoreValueType_t xType,
                                 size_t xLength,
                                 const void * pvData )
{
    char pcFileName[ KVSTORE_MAX_FNANME ] = { 0 };

    lfs_t * pLfsCtx = pxGetDefaultFsCtx();

    lfs_ssize_t lReturn = LFS_ERR_INVAL;

    lfs_file_t xFile = { 0 };

    BaseType_t xFileOpenFlag = pdFALSE;

    if( pvData != NULL )
    {
        /* Construct file name */
        ( void ) strncpy( pcFileName, KVSTORE_PREFIX, KVSTORE_MAX_FNANME );
        ( void ) strncat( pcFileName, kvStoreKeyMap[ xKey ], KVSTORE_MAX_FNANME );

        /* Open the file */
        lReturn = lfs_file_open( pLfsCtx, &xFile, pcFileName, LFS_O_WRONLY | LFS_O_TRUNC | LFS_O_CREAT );

        if( lReturn != LFS_ERR_OK )
        {
            LogError( "Error while opening file: %s.", pcFileName );
        }
        else /* Write the header if opened successfully */
        {
            xFileOpenFlag = pdTRUE;
            KVStoreTLVHeader_t xTlvHeader =
            {
                .type   = xType,
                .length = xLength
            };

            lReturn = lfs_file_write( pLfsCtx, &xFile, &xTlvHeader, sizeof( KVStoreTLVHeader_t ) );
            vLfsSSizeToErr( &lReturn, sizeof( KVStoreTLVHeader_t ) );
        }

        if( lReturn != LFS_ERR_OK )
        {
            LogError( "Error while writing KVStoreTLVHeader_t of length %ld bytes to file: %s.",
                      sizeof( KVStoreTLVHeader_t ), pcFileName );
        }
        else /* Write the data */
        {
            lReturn = lfs_file_write( pLfsCtx, &xFile, pvData, xLength );
            vLfsSSizeToErr( &lReturn, xLength );
        }

        if( lReturn != LFS_ERR_OK )
        {
            LogError( "Error while writing data of length %ld bytes to file: %s.",
                      xLength, pcFileName );
        }

        if( xFileOpenFlag == pdTRUE )
        {
            ( void ) lfs_file_sync( pLfsCtx, &xFile );
            ( void ) lfs_file_close( pLfsCtx, &xFile );

            /* Delete partially written file if writing was not successful */
            if( lReturn != LFS_ERR_OK )
            {
                ( void ) lfs_remove( pLfsCtx, pcFileName );
            }
        }
    }

    return( lReturn == LFS_ERR_OK );
}

void vprvNvImplInit( void )
{
    /*TODO: Wait for filesystem initialization */
}
#endif /* KV_STORE_NVIMPL_LITTLEFS */
