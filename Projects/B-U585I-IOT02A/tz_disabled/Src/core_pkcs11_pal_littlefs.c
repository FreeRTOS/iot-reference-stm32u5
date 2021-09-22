/*
 * FreeRTOS STM32 Reference Integration
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
 */

/**
 * @file core_pkcs11_pal.c
 * @brief Littlefs file save and read implementation
 * for PKCS #11 based on mbedTLS with for software keys. This
 * file deviates from the FreeRTOS style standard for some function names and
 * data types in order to maintain compliance with the PKCS #11 standard.
 */
/*-----------------------------------------------------------*/

/* PKCS 11 includes. */
#include "core_pkcs11_config.h"
#include "core_pkcs11_config_defaults.h"
#include "core_pkcs11.h"
#include "core_pkcs11_pal_utils.h"

#include "main.h"

#include "lfs_util.h"
#include "lfs.h"
#include "lfs_port.h"

/*-----------------------------------------------------------*/

static lfs_t * pLfsCtx = NULL;

/*-----------------------------------------------------------*/
/**
 * @brief Checks to see if a file exists
 *
 * @param[in] pcFileName         The name of the file to check for existence.
 *
 * @returns CKR_OK if the file exists, CKR_OBJECT_HANDLE_INVALID if not.
 */
static CK_RV prvFileExists( const char * pcFileName )
{
    int lReturn = 0;
    CK_RV xReturn = CKR_OK;
    struct lfs_info xFileInfo = { 0 };

    configASSERT( pcFileName != NULL );

    lReturn = lfs_stat( pLfsCtx,  pcFileName, &xFileInfo );

    if( lReturn < 0 ||
        ( xFileInfo.type & LFS_TYPE_REG ) == 0 )
    {
        xReturn = CKR_OBJECT_HANDLE_INVALID;
        LogInfo( ( "Could not open %s for reading.", pcFileName ) );
    }
    else
    {
        xReturn = CKR_OK;
        LogDebug( ( "Found file %s with size: %d", pcFileName, xLfsInfo.size ) );
    }

    return xReturn;
}

/**
 * @brief Reads object value from file system.
 *
 * @param[in] pcLabel            The PKCS #11 label to convert to a file name
 * @param[in] pcFileName        The name of the file to check for existence.
 * @param[out] pHandle           The type of the PKCS #11 object.
 *
 */
static CK_RV prvReadData( const char * pcFileName,
                          CK_BYTE_PTR * ppucData,
                          CK_ULONG_PTR pulDataSize )
{
    CK_RV xReturn = CKR_OK;
    int lReturn = 0;

    struct lfs_info xFileInfo = { 0 };
    lfs_file_t xFile = { 0 };
    BaseType_t xFileOpenedFlag = pdFALSE;

    /* Initialize return vars */
    *ppucData = NULL;
    *pulDataSize = 0;

    lReturn = lfs_stat( pLfsCtx,  pcFileName, &xFileInfo );

    if( lReturn < 0 ||
        ( xFileInfo.type & LFS_TYPE_REG ) == 0 ||
         xFileInfo.size == 0 )
    {
        LogError( ( "PKCS #11 PAL failed to get object value. "
                    "Could not locate file named %s for reading. rc: %d, size: %d, type: %d",
                    pcFileName, lReturn, xFileInfo.size, xFileInfo.type ) );
        lReturn = -1;
        xReturn = CKR_FUNCTION_FAILED;
    }
    else
    {
        lReturn = lfs_file_open( pLfsCtx, &xFile, pcFileName, LFS_O_RDONLY );
        xFileOpenedFlag = pdTRUE;
    }

    if( xReturn == CKR_OK &&
        lReturn < 0 )
    {
        LogError( ( "PKCS #11 PAL failed to get object value. "
                    "Could not open file named %s for reading. rc: %d",
                    pcFileName, lReturn ) );
        lReturn = 0;
        xReturn = CKR_FUNCTION_FAILED;
    }
    else /* File opened, so allocate memory */
    {
        *pulDataSize = xFileInfo.size;
        *ppucData = pvPortMalloc( xFileInfo.size );
    }

    if( xReturn == CKR_OK &&
        *ppucData == NULL )
    {
        *pulDataSize = 0;
        LogError( ( "PKCS #11 PAL failed to get object value. "
                    "Failed to allocate %d bytes",
                    xFileInfo.size ) );
        xReturn = CKR_HOST_MEMORY;
    }
    else if( *ppucData != NULL )
    {
        lReturn = lfs_file_read( pLfsCtx, &xFile, *ppucData, *pulDataSize );
    }

    if( lReturn < 0 )
    {
        LogError( ( "PKCS #11 PAL failed to get object value. "
                    "Could not read file named %s. rc: %d",
                    pcFileName, lReturn ) );

        vPortFree( *ppucData );
        *ppucData = NULL;
        *pulDataSize = 0;

        lReturn = 0;
        xReturn = CKR_FUNCTION_FAILED;
    }
    else if( lReturn != *pulDataSize )
    {
        LogError( ( "PKCS #11 PAL Failed to get object value. Expected to read %ld "
                    "from %s but received %ld", *pulDataSize, pcFileName, lReturn ) );

        vPortFree( *ppucData );
        *ppucData = NULL;
        *pulDataSize = 0;

        lReturn = 0;
        xReturn = CKR_FUNCTION_FAILED;
    }

    if( xFileOpenedFlag )
    {
        ( void ) lfs_file_close( pLfsCtx, &xFile );
    }

    return xReturn;
}

/*-----------------------------------------------------------*/

CK_RV PKCS11_PAL_Initialize( void )
{
    pLfsCtx = pxGetDefaultFsCtx();
    return CKR_OK;
}

CK_OBJECT_HANDLE PKCS11_PAL_SaveObject( CK_ATTRIBUTE_PTR pxLabel,
                                        CK_BYTE_PTR pucData,
                                        CK_ULONG ulDataSize )
{
    lfs_file_t xFile = { 0 };
    int lResult = 0;

    lfs_ssize_t lBytesWritten;
    const char * pcFileName = NULL;
    CK_OBJECT_HANDLE xHandle = ( CK_OBJECT_HANDLE ) eInvalidHandle;

    if( ( pxLabel != NULL ) && ( pucData != NULL ) )
    {
        /* Converts a label to its respective filename and handle. */
        PAL_UTILS_LabelToFilenameHandle( pxLabel->pValue,
                                         &pcFileName,
                                         &xHandle );
    }
    else
    {
        LogError( ( "Could not save object. Received invalid parameters." ) );
    }

    if( pcFileName != NULL )
    {
        ( void ) lfs_remove( pLfsCtx, pcFileName );
        /* Overwrite the file every time it is saved. */
        lResult = lfs_file_open( pLfsCtx, &xFile, pcFileName, LFS_O_WRONLY | LFS_O_CREAT );

        if( lResult < 0 )
        {
            LogError( ( "PKCS #11 PAL was unable to save object to file. "
                        "The PAL was unable to open a file with name %s in write mode.", pcFileName ) );
            xHandle = ( CK_OBJECT_HANDLE ) eInvalidHandle;
        }
        else
        {
            lBytesWritten = lfs_file_write( pLfsCtx, &xFile, pucData, ulDataSize );

            if( lBytesWritten != ulDataSize )
            {
                LogError( ( "PKCS #11 PAL was unable to save object to file. "
                            "Expected to write %lu bytes, but wrote %lu bytes.", ulDataSize, lBytesWritten ) );
                xHandle = ( CK_OBJECT_HANDLE ) eInvalidHandle;
            }
            else
            {
                LogDebug( ( "Successfully wrote %lu to %s", ulBytesWritten, pcFileName ) );
            }

            lResult = lfs_file_sync( pLfsCtx, &xFile );
            if( lResult < 0 )
            {
                LogError( ( "PKCS #11 PAL was unable to save object to file. "
                            "Failed to commit changes to flash. rc: %ld", lResult ) );
                xHandle = ( CK_OBJECT_HANDLE ) eInvalidHandle;
            }
            ( void ) lfs_file_close( pLfsCtx, &xFile );
        }
    }
    else
    {
        LogError( ( "Could not save object. Unable to open the correct file." ) );
    }

    return xHandle;
}

/*-----------------------------------------------------------*/


CK_OBJECT_HANDLE PKCS11_PAL_FindObject( CK_BYTE_PTR pxLabel,
                                        CK_ULONG usLength )
{
    const char * pcFileName = NULL;
    CK_OBJECT_HANDLE xHandle = ( CK_OBJECT_HANDLE ) eInvalidHandle;

    ( void ) usLength;

    if( pxLabel != NULL )
    {
        PAL_UTILS_LabelToFilenameHandle( ( const char * ) pxLabel,
                                         &pcFileName,
                                         &xHandle );

        if( CKR_OK != prvFileExists( pcFileName ) )
        {
            xHandle = ( CK_OBJECT_HANDLE ) eInvalidHandle;
        }
    }
    else
    {
        LogError( ( "Could not find object. Received a NULL label." ) );
    }

    return xHandle;
}
/*-----------------------------------------------------------*/

CK_RV PKCS11_PAL_GetObjectValue( CK_OBJECT_HANDLE xHandle,
                                 CK_BYTE_PTR * ppucData,
                                 CK_ULONG_PTR pulDataSize,
                                 CK_BBOOL * pIsPrivate )
{
    CK_RV xReturn = CKR_OK;
    const char * pcFileName = NULL;


    if( ( ppucData == NULL ) || ( pulDataSize == NULL ) || ( pIsPrivate == NULL ) )
    {
        xReturn = CKR_ARGUMENTS_BAD;
        LogError( ( "Could not get object value. Received a NULL argument." ) );
    }
    else
    {
        xReturn = PAL_UTILS_HandleToFilename( xHandle, &pcFileName, pIsPrivate );
    }

    if( xReturn == CKR_OK )
    {
        xReturn = prvReadData( pcFileName, ppucData, pulDataSize );
    }

    return xReturn;
}

/*-----------------------------------------------------------*/

void PKCS11_PAL_GetObjectValueCleanup( CK_BYTE_PTR pucData,
                                       CK_ULONG ulDataSize )
{
    /* Unused parameters. */
    ( void ) ulDataSize;

    if( NULL != pucData )
    {
        vPortFree( pucData );
    }
}

/*-----------------------------------------------------------*/

CK_RV PKCS11_PAL_DestroyObject( CK_OBJECT_HANDLE xHandle )
{
    const char * pcFileName = NULL;
    CK_BBOOL xIsPrivate = CK_TRUE;
    CK_RV xResult = CKR_OBJECT_HANDLE_INVALID;
    int ret = 0;

    xResult = PAL_UTILS_HandleToFilename( xHandle,
                                          &pcFileName,
                                          &xIsPrivate );

    if( ( xResult == CKR_OK ) &&
        ( prvFileExists( pcFileName ) == CKR_OK ) )
    {
        ret = lfs_remove( pLfsCtx, pcFileName );

        if( ret != 0 )
        {
            xResult = CKR_FUNCTION_FAILED;
        }
    }

    return xResult;
}

/*-----------------------------------------------------------*/
