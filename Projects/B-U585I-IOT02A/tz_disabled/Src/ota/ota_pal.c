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
 * @file ota_pal.c Contains non-secure side platform abstraction layer implementations
 * for AWS OTA update library.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ota.h"
#include "ota_pal.h"
#include "stm32u5xx.h"
#include "stm32u5xx_hal_flash.h"
#include "lfs.h"
#include "lfs_port.h"

const char OTA_JsonFileSignatureKey[ OTA_FILE_SIG_KEY_STR_MAX_LENGTH ] = "sig-sha256-ecdsa";

#define FLASH_START_CURRENT_BANK    ( ( uint32_t ) FLASH_BASE )

#define FLASH_START_PASSIVE_BANK    ( ( uint32_t ) ( FLASH_BASE + FLASH_BANK_SIZE ) )

#define NUM_QUAD_WORDS( length )         ( length >> 4UL )

#define NUM_REMAINING_BYTES( length )    ( length & 0x0F )

#define ALTERNATE_BANK( bank )           ( ( bank == FLASH_BANK_1 ) ? FLASH_BANK_2 : FLASH_BANK_1 )

#define IMAGE_CONTEXT_FILE_NAME    "/ota/image_state"

typedef enum OtaPalImageStateInternal
{
    OTA_PAL_IMAGE_STATE_UNKNOWN = 0,
    OTA_PAL_IMAGE_STATE_PROGRAMMING,
    OTA_PAL_IMAGE_STATE_PENDING_COMMIT,
    OTA_PAL_IMAGE_STATE_COMMITTED,
    OTA_PAL_IMAGE_STATE_INVALID
} OtaPalImageStateInternal_t;

typedef struct OtaImageContext
{
    uint32_t ulBank;
    uint32_t ulBaseAddress;
    uint32_t ulImageSize;
    OtaPalImageStateInternal_t state;
} OtaImageContext_t;

static FLASH_OBProgramInitTypeDef OBInit;

static BaseType_t prvLoadImageContextFromFlash( OtaImageContext_t * pContext )
{
    lfs_ssize_t lReturn = LFS_ERR_CORRUPT;
    BaseType_t status = pdFALSE;
    lfs_t * pLfsCtx = pxGetDefaultFsCtx();
    const char * pcFileName = IMAGE_CONTEXT_FILE_NAME;

    lfs_file_t xFile = { 0 };

    /* Open the file */
    lReturn = lfs_file_open( pLfsCtx, &xFile, pcFileName, LFS_O_RDONLY );

    /* Read the header */
    if( lReturn == LFS_ERR_OK )
    {
        lReturn = lfs_file_read( pLfsCtx, &xFile, pContext, sizeof( OtaImageContext_t ) );

        if( lReturn == sizeof( OtaImageContext_t ) )
        {
            status = pdTRUE;
        }
        else
        {
            LogError( ( " Failed to read OTA image context from file %s, error = %d.\r\n ", pcFileName, lReturn ) );
        }

        ( void ) lfs_file_close( pLfsCtx, &xFile );
    }

    return status;
}

static BaseType_t prvSaveImageContextToFlash( OtaImageContext_t * pContext )
{
    lfs_ssize_t lReturn = LFS_ERR_CORRUPT;
    BaseType_t status = pdFALSE;
    lfs_t * pLfsCtx = pxGetDefaultFsCtx();
    const char * pcFileName = IMAGE_CONTEXT_FILE_NAME;

    lfs_file_t xFile = { 0 };

    /* Open the file */
    lReturn = lfs_file_open( pLfsCtx, &xFile, pcFileName, ( LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC ) );

    /* Read the header */
    if( lReturn == LFS_ERR_OK )
    {
        lReturn = lfs_file_write( pLfsCtx, &xFile, pContext, sizeof( OtaImageContext_t ) );

        if( lReturn == sizeof( OtaImageContext_t ) )
        {
            status = pdTRUE;
        }
        else
        {
            LogError( ( "Failed to save OTA image context to file %s, error = %d.\r\n", pcFileName, lReturn ) );
        }

        ( void ) lfs_file_close( pLfsCtx, &xFile );
    }
    else
    {
        LogError( ( "Failed to open file %s to save OTA image context, error = %d.\r\n ", pcFileName, lReturn ) );
    }

    return status;
}

static BaseType_t prvRemoveImageContextFromFlash( void )
{
    lfs_ssize_t lReturn = LFS_ERR_CORRUPT;
    BaseType_t status = pdFALSE;
    lfs_t * pLfsCtx = pxGetDefaultFsCtx();
    const char * pcFileName = IMAGE_CONTEXT_FILE_NAME;
    struct lfs_info xFileInfo = { 0 };

    if( lfs_stat( pLfsCtx, pcFileName, &xFileInfo ) == LFS_ERR_OK )
    {
        /* Open the file */
        lReturn = lfs_remove( pLfsCtx, pcFileName );

        if( lReturn == LFS_ERR_OK )
        {
            status = pdTRUE;
        }
    }
    else
    {
        status = pdTRUE;
    }

    return status;
}


static OtaImageContext_t * prvGetImageContext( void )
{
    static OtaImageContext_t imageContext = { 0 };

    if( imageContext.state == OTA_PAL_IMAGE_STATE_UNKNOWN )
    {
        ( void ) prvLoadImageContextFromFlash( &imageContext );
    }

    return &imageContext;
}


static HAL_StatusTypeDef prvFlashSetDualBankMode( void )
{
    HAL_StatusTypeDef status = HAL_ERROR;

    /* Allow Access to Flash control registers and user Flash */
    status = HAL_FLASH_Unlock();

    if( status == HAL_OK )
    {
        /* Allow Access to option bytes sector */
        status = HAL_FLASH_OB_Unlock();

        if( status == HAL_OK )
        {
            /* Get the Dual bank configuration status */
            HAL_FLASHEx_OBGetConfig( &OBInit );

            if( ( OBInit.USERConfig & OB_DUALBANK_DUAL ) != OB_DUALBANK_DUAL )
            {
                OBInit.OptionType = OPTIONBYTE_USER;
                OBInit.USERType = OB_USER_DUALBANK;
                OBInit.USERConfig = OB_DUALBANK_DUAL;
                status = HAL_FLASHEx_OBProgram( &OBInit );
            }

            ( void ) HAL_FLASH_OB_Lock();
        }

        ( void ) HAL_FLASH_Lock();
    }

    return status;
}

static HAL_StatusTypeDef prvSwapBankAndBoot( void )
{
    HAL_StatusTypeDef status = HAL_ERROR;

    /* Allow Access to Flash control registers and user Flash */
    status = HAL_FLASH_Unlock();

    if( status == HAL_OK )
    {
        /* Allow Access to option bytes sector */
        status = HAL_FLASH_OB_Unlock();

        if( status == HAL_OK )
        {
            /* Get the Dual boot configuration status */
            HAL_FLASHEx_OBGetConfig( &OBInit );

            OBInit.OptionType = OPTIONBYTE_USER;
            OBInit.USERType = OB_USER_SWAP_BANK;

            if( ( ( OBInit.USERConfig ) & ( OB_SWAP_BANK_ENABLE ) ) != OB_SWAP_BANK_ENABLE )
            {
                OBInit.USERConfig = OB_SWAP_BANK_ENABLE;
            }
            else
            {
                OBInit.USERConfig = OB_SWAP_BANK_DISABLE;
            }

            status = HAL_FLASHEx_OBProgram( &OBInit );

            if( status == HAL_OK )
            {
                /* Generate System Reset to load the new option byte values ***************/
                /* On successful option bytes loading the system should reset and control should not return from this function. */
                status = HAL_FLASH_OB_Launch();
            }

            ( void ) HAL_FLASH_OB_Lock();
        }

        ( void ) HAL_FLASH_Lock();
    }

    return status;
}

static uint32_t prvGetActiveBank( void )
{
    HAL_StatusTypeDef status = HAL_ERROR;
    uint32_t ulBank = 0UL;

    status = HAL_FLASH_Unlock();

    if( status == HAL_OK )
    {
        status = HAL_FLASH_OB_Unlock();

        if( status == HAL_OK )
        {
            /* Get the Dual boot configuration status */
            HAL_FLASHEx_OBGetConfig( &OBInit );
            ulBank = ( ( OBInit.USERConfig & OB_SWAP_BANK_ENABLE ) == OB_SWAP_BANK_ENABLE ) ? FLASH_BANK_2 : FLASH_BANK_1;

            ( void ) HAL_FLASH_OB_Lock();
        }

        ( void ) HAL_FLASH_Lock();
    }

    return ulBank;
}

static uint32_t prvGetPassiveBank( void )
{
    uint32_t activeBank = prvGetActiveBank();

    return ALTERNATE_BANK( activeBank );
}


static HAL_StatusTypeDef prvWriteToFlash( uint32_t destination,
                                          uint8_t * pSource,
                                          uint32_t length )
{
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t i = 0U;
    uint8_t quadWord[ 16 ] = { 0 };
    uint32_t numQuadWords = NUM_QUAD_WORDS( length );
    uint32_t remainingBytes = NUM_REMAINING_BYTES( length );

    /* Unlock the Flash to enable the flash control register access *************/
    HAL_FLASH_Unlock();

    for( i = 0U; i < numQuadWords; i++ )
    {
        /* Device voltage range supposed to be [2.7V to 3.6V], the operation will
         * be done by word */

        memcpy( quadWord, pSource, 16UL );
        status = HAL_FLASH_Program( FLASH_TYPEPROGRAM_QUADWORD, destination, ( uint32_t ) quadWord );

        if( status == HAL_OK )
        {
            /* Check the written value */
            if( memcmp( ( void * ) destination, quadWord, 16UL ) != 0 )
            {
                /* Flash content doesn't match SRAM content */
                status = HAL_ERROR;
            }
        }

        if( status == HAL_OK )
        {
            /* Increment FLASH destination address and the source address. */
            destination += 16UL;
            pSource += 16UL;
        }
        else
        {
            break;
        }
    }

    if( status == HAL_OK )
    {
        if( remainingBytes > 0 )
        {
            memcpy( quadWord, pSource, remainingBytes );
            memset( ( quadWord + remainingBytes ), 0xFF, ( 16UL - remainingBytes ) );

            status = HAL_FLASH_Program( FLASH_TYPEPROGRAM_QUADWORD, destination, ( uint32_t ) quadWord );

            if( status == HAL_OK )
            {
                /* Check the written value */
                if( memcmp( ( void * ) destination, quadWord, 16UL ) != 0 )
                {
                    /* Flash content doesn't match SRAM content */
                    status = HAL_ERROR;
                }
            }
        }
    }

    /* Lock the Flash to disable the flash control register access (recommended
    *  to protect the FLASH memory against possible unwanted operation) *********/
    HAL_FLASH_Lock();

    return status;
}

static HAL_StatusTypeDef prvEraseBank( uint32_t bankNumber )
{
    uint32_t errorCode = 0U, pageError = 0U;
    FLASH_EraseInitTypeDef pEraseInit;
    HAL_StatusTypeDef status = HAL_ERROR;

    /* Unlock the Flash to enable the flash control register access *************/
    status = HAL_FLASH_Unlock();

    if( status == HAL_OK )
    {
        pEraseInit.Banks = bankNumber;
        pEraseInit.NbPages = FLASH_PAGE_NB;
        pEraseInit.Page = 0U;
        pEraseInit.TypeErase = FLASH_TYPEERASE_MASSERASE;

        status = HAL_FLASHEx_Erase( &pEraseInit, &pageError );

        /* Lock the Flash to disable the flash control register access (recommended
        *  to protect the FLASH memory against possible unwanted operation) *********/
        ( void ) HAL_FLASH_Lock();

        if( status != HAL_OK )
        {
            errorCode = HAL_FLASH_GetError();
            LogError( ( "Failed to erase the flash bank, errorCode = %u, pageError = %u.\r\n", errorCode, pageError ) );
        }
    }
    else
    {
        LogError( ( "Failed to lock flash for erase, errorCode = %u.\r\n", HAL_FLASH_GetError() ) );
    }

    return status;
}


OtaPalStatus_t xOtaPalCreateImage( OtaFileContext_t * const pFileContext )
{
    OtaPalStatus_t otaStatus = OtaPalRxFileCreateFailed;
    HAL_StatusTypeDef status = HAL_ERROR;
    uint32_t ulPassiveBank = prvGetPassiveBank();
    OtaImageContext_t * pContext = prvGetImageContext();


    if( ( pFileContext->pFile == NULL ) &&
        ( pFileContext->fileSize <= FLASH_BANK_SIZE ) &&
        ( pContext->state != OTA_PAL_IMAGE_STATE_PROGRAMMING ) )
    {
        /* Set dual bank mode if not already set. */
        status = prvFlashSetDualBankMode();

        if( status == HAL_OK )
        {
            /* Get Alternate bank and erase the flash. */
            status = prvEraseBank( ulPassiveBank );
        }

        if( status == HAL_OK )
        {
            pContext->ulBank = ulPassiveBank;
            pContext->ulBaseAddress = FLASH_START_PASSIVE_BANK;
            pContext->ulImageSize = pFileContext->fileSize;
            pContext->state = OTA_PAL_IMAGE_STATE_PROGRAMMING;
            pFileContext->pFile = ( uint8_t * ) pContext;
            otaStatus = OtaPalSuccess;
        }
    }

    return otaStatus;
}

int16_t iOtaPalWriteImageBlock( OtaFileContext_t * const pFileContext,
                                uint32_t offset,
                                uint8_t * const pData,
                                uint32_t blockSize )
{
    int16_t bytesWritten = 0;
    HAL_StatusTypeDef status = HAL_ERROR;
    OtaImageContext_t * pContext = prvGetImageContext();

    if( ( pFileContext->pFile == ( uint8_t * ) ( pContext ) ) &&
        ( ( offset + blockSize ) <= pContext->ulImageSize ) &&
        ( pContext->state == OTA_PAL_IMAGE_STATE_PROGRAMMING ) )
    {
        status = prvWriteToFlash( ( pContext->ulBaseAddress + offset ), pData, blockSize );

        if( status == HAL_OK )
        {
            bytesWritten = blockSize;
        }
    }

    return bytesWritten;
}

OtaPalStatus_t xOtaPalFinalizeImage( OtaFileContext_t * const pFileContext )
{
    OtaPalStatus_t otaStatus = OtaPalCommitFailed;
    OtaImageContext_t * pContext = prvGetImageContext();

    if( ( pFileContext->pFile == ( uint8_t * ) ( pContext ) ) &&
        ( pContext->state == OTA_PAL_IMAGE_STATE_PROGRAMMING ) )

    {
        /* TODO: signature validation and confirm image here. */
        pContext->state = OTA_PAL_IMAGE_STATE_PENDING_COMMIT;
        otaStatus = OtaPalSuccess;
    }

    return otaStatus;
}


OtaPalStatus_t xOtaPalAbortImage( OtaFileContext_t * const pFileContext )
{
    OtaImageContext_t * pContext = prvGetImageContext();

    if( pContext->state != OTA_PAL_IMAGE_STATE_UNKNOWN )
    {
        /*
         * Erase the bank and set the image state to invalid, if its being programmed or
         * pending reboot after validation.
         */
        if( pFileContext->pFile != NULL )
        {
            ( void ) prvEraseBank( pContext->ulBank );
            pFileContext->pFile = NULL;
        }

        pContext->state = OTA_PAL_IMAGE_STATE_INVALID;
    }

    return OtaPalSuccess;
}

OtaPalStatus_t xOtaPalActivateImage( OtaFileContext_t * const pFileContext )
{
    OtaPalStatus_t otaStatus = OtaPalActivateFailed;
    BaseType_t status = pdFALSE;
    OtaImageContext_t * pContext = prvGetImageContext();

    if( ( pContext->state == OTA_PAL_IMAGE_STATE_PENDING_COMMIT ) &&
        ( pContext->ulBank == prvGetPassiveBank() ) )
    {
        /** Save current image context to flash so as to fetch the context after boot. */
        status = prvSaveImageContextToFlash( pContext );

        if( status == pdTRUE )
        {
            ( void ) prvSwapBankAndBoot();

            /** Boot failed. Removed the save context from flash. */
            ( void ) prvRemoveImageContextFromFlash();
        }
    }

    return otaStatus;
}

OtaPalStatus_t xOtaPalSetImageState( OtaFileContext_t * const pFileContext,
                                     OtaImageState_t eState )
{
    OtaPalStatus_t otaStatus = OtaPalBadImageState;
    OtaImageContext_t * pContext = prvGetImageContext();
    uint32_t activeBank = prvGetActiveBank();
    uint32_t passiveBank = ALTERNATE_BANK( activeBank );

    if( pContext->state != OTA_PAL_IMAGE_STATE_UNKNOWN )
    {
        switch( eState )
        {
            case OtaImageStateTesting:

                if( ( pContext->state == OTA_PAL_IMAGE_STATE_PENDING_COMMIT ) &&
                    ( pContext->ulBank == activeBank ) )
                {
                    /** New image bank is booted successfully and it's pending for commit. */
                    otaStatus = OtaPalSuccess;
                }

                break;

            case OtaImageStateAccepted:

                if( ( pContext->state == OTA_PAL_IMAGE_STATE_PENDING_COMMIT ) &&
                    ( pContext->ulBank == activeBank ) )
                {
                    /** New image bank is booted successfully and it have passed self test. Make it as accepted
                     * by removing the image context from flash and setting image state to valid. */

                    prvRemoveImageContextFromFlash();

                    ( void ) prvEraseBank( passiveBank );
                    pContext->state = OTA_PAL_IMAGE_STATE_COMMITTED;
                    otaStatus = OtaPalSuccess;
                }
                else
                {
                    otaStatus = OtaPalCommitFailed;
                }

                break;

            case OtaImageStateRejected:

                /* Make sure the image is not committed already. A committed image cannot be aborted or rejected. */
                if( pContext->state != OTA_PAL_IMAGE_STATE_COMMITTED )
                {
                    /*
                     *  Remove the persisted state of image from flash.
                     */
                    prvRemoveImageContextFromFlash();
                    pContext->state = OTA_PAL_IMAGE_STATE_INVALID;
                    otaStatus = OtaPalSuccess;
                }
                else
                {
                    otaStatus = OtaPalRejectFailed;
                }

                break;

            case OtaImageStateAborted:

                /* Make sure the image is not committed already. A committed image cannot be aborted or rejected. */
                if( pContext->state != OTA_PAL_IMAGE_STATE_COMMITTED )
                {
                    /*
                     *  Remove the persisted state of image from flash.
                     */
                    prvRemoveImageContextFromFlash();
                    pContext->state = OTA_PAL_IMAGE_STATE_INVALID;
                    otaStatus = OtaPalSuccess;
                }
                else
                {
                    otaStatus = OtaPalAbortFailed;
                }

                break;

            default:
                LogError( ( "Unknown state transition, current state = %d, expected state = %d.\r\n", pContext->state, eState ) );
                break;
        }
    }

    return otaStatus;
}


OtaPalImageState_t xOtaPalGetImageState( OtaFileContext_t * const pFileContext )
{
    OtaPalImageState_t state = OtaPalImageStateUnknown;
    OtaImageContext_t * pContext = prvGetImageContext();

    switch( pContext->state )
    {
        case OTA_PAL_IMAGE_STATE_PENDING_COMMIT:
            state = OtaPalImageStatePendingCommit;
            break;

        case OTA_PAL_IMAGE_STATE_COMMITTED:
            state = OtaPalImageStateValid;
            break;

        case OTA_PAL_IMAGE_STATE_INVALID:
            state = OtaPalImageStateInvalid;
            break;

        case OTA_PAL_IMAGE_STATE_PROGRAMMING:
        case OTA_PAL_IMAGE_STATE_UNKNOWN:
        default:
            state = OtaPalImageStateUnknown;
            break;
    }

    return state;
}


OtaPalStatus_t xOtaPalResetDevice( OtaFileContext_t * const pFileContext )
{
    OtaImageContext_t * pContext = prvGetImageContext();
    uint32_t activeBank = prvGetActiveBank();

    if( ( pContext->state != OTA_PAL_IMAGE_STATE_COMMITTED ) &&
        ( pContext->ulBank == activeBank ) )
    {
        prvSwapBankAndBoot();
    }
    else
    {
        /*
         * TODO: Just Boot.
         */
    }

    return OtaPalUninitialized;
}
