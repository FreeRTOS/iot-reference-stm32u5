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

#include "logging_levels.h"
#define LOG_LEVEL    LOG_INFO
#include "logging.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ota.h"
#include "ota_pal.h"
#include "stm32u5xx.h"
#include "stm32u5xx_hal_flash.h"
#include "lfs.h"
#include "fs/lfs_port.h"

#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls_error_utils.h"

#include "PkiObject.h"

#define FLASH_START_INACTIVE_BANK    ( ( uint32_t ) ( FLASH_BASE + FLASH_BANK_SIZE ) )

#define NUM_QUAD_WORDS( length )         ( length >> 4UL )

#define NUM_REMAINING_BYTES( length )    ( length & 0x0F )

#define IMAGE_CONTEXT_FILE_NAME    "/ota/image_state"

#define OTA_IMAGE_MIN_SIZE         ( 16 )


typedef enum
{
    OTA_PAL_NOT_INITIALIZED = 0,
    OTA_PAL_READY,
    OTA_PAL_FILE_OPEN,
    OTA_PAL_PENDING_ACTIVATION,
    OTA_PAL_PENDING_SELF_TEST,
    OTA_PAL_NEW_IMAGE_BOOTED,
    OTA_PAL_NEW_IMAGE_WDT_RESET,
    OTA_PAL_SELF_TEST_FAILED,
    OTA_PAL_ACCEPTED,
    OTA_PAL_REJECTED,
    OTA_PAL_INVALID
} OtaPalState_t;

static const char * ppcPalStateString[] =
{
    "Not Initialized",
    "Ready",
    "File Open",
    "Pending Activation",
    "Pending Self Test",
    "New Image Booted",
    "Watchdog Reset",
    "Self Test Failed",
    "Accepted",
    "Rejected",
    "Invalid"
};

typedef struct
{
    OtaPalState_t xPalState;
    uint32_t ulFileTargetBank;
} OtaPalNvContext_t;

typedef struct
{
    uint32_t ulTargetBank;
    uint32_t ulPendingBank;
    uint32_t ulBaseAddress;
    uint32_t ulImageSize;
    OtaPalState_t xPalState;
} OtaPalContext_t;


const char OTA_JsonFileSignatureKey[] = "sig-sha256-ecdsa";

static OtaPalContext_t xPalContext =
{
    .xPalState     = OTA_PAL_NOT_INITIALIZED,
    .ulTargetBank  = 0,
    .ulPendingBank = 0,
    .ulBaseAddress = 0,
    .ulImageSize   = 0,
};

static uint32_t ulBankAtBootup = 0;

/* Static function forward declarations */

/* Load/Save/Delete */
static BaseType_t prvInitializePalContext( OtaPalContext_t * pxContext );
static BaseType_t prvWritePalNvContext( OtaPalContext_t * pxContext );
static BaseType_t prvDeletePalNvContext( void );
static OtaPalContext_t * prvGetImageContext( void );

/* Active / Inactive bank helpers */
static uint32_t prvGetActiveBank( void );
static uint32_t prvGetInactiveBank( void );

/* STM32 HAL Wrappers */
static HAL_StatusTypeDef prvFlashSetDualBankMode( void );
static BaseType_t prvSelectBank( uint32_t ulNewBank );
static uint32_t prvGetBankSettingFromOB( void );

static void prvOptionByteApply( void );

/* Flash write./erase */
static HAL_StatusTypeDef prvWriteToFlash( uint32_t destination,
                                          uint8_t * pSource,
                                          uint32_t length );

static BaseType_t prvEraseBank( uint32_t bankNumber );

/* Verify signature */
static OtaPalStatus_t prvValidateSignature( const char * pcPubKeyLabel,
                                            const unsigned char * pucSignature,
                                            const size_t uxSignatureLength,
                                            const unsigned char * pucImageHash,
                                            const size_t uxHashLength );

static BaseType_t xCalculateImageHash( const unsigned char * pucImageAddress,
                                       const size_t uxImageLength,
                                       unsigned char * pucHashBuffer,
                                       size_t uxHashBufferLength,
                                       size_t * puxHashLength );

const char * otaImageStateToString( OtaImageState_t xState )
{
    const char * pcStateString;

    switch( xState )
    {
        case OtaImageStateTesting:
            pcStateString = "OtaImageStateTesting";
            break;

        case OtaImageStateAccepted:
            pcStateString = "OtaImageStateAccepted";
            break;

        case OtaImageStateRejected:
            pcStateString = "OtaImageStateRejected";
            break;

        case OtaImageStateAborted:
            pcStateString = "OtaImageStateAborted";
            break;

        default:
        case OtaImageStateUnknown:
            pcStateString = "OtaImageStateUnknown";
            break;
    }

    return pcStateString;
}

const char * otaPalImageStateToString( xPalImageState )
{
    const char * pcPalImageState;

    switch( xPalImageState )
    {
        case OtaPalImageStatePendingCommit:
            pcPalImageState = "OtaPalImageStatePendingCommit";
            break;

        case OtaPalImageStateValid:
            pcPalImageState = "OtaPalImageStateValid";
            break;

        case OtaPalImageStateInvalid:
            pcPalImageState = "OtaPalImageStateInvalid";
            break;

        default:
        case OtaPalImageStateUnknown:
            pcPalImageState = "OtaPalImageStateUnknown";
            break;
    }

    return pcPalImageState;
}

static const char * pcPalStateToString( OtaPalState_t xPalState )
{
    const char * pcStateString = "";

    if( xPalState <= OTA_PAL_INVALID )
    {
        pcStateString = ppcPalStateString[ xPalState ];
    }

    return pcStateString;
}

static inline uint32_t ulGetOtherBank( uint32_t ulBankNumber )
{
    uint32_t ulRevertBank;

    if( ulBankNumber == FLASH_BANK_2 )
    {
        ulRevertBank = FLASH_BANK_1;
    }
    else if( ulBankNumber == FLASH_BANK_1 )
    {
        ulRevertBank = FLASH_BANK_2;
    }
    else
    {
        ulRevertBank = 0;
    }

    return ulRevertBank;
}

static BaseType_t prvInitializePalContext( OtaPalContext_t * pxContext )
{
    BaseType_t xResult = pdTRUE;
    lfs_t * pxLfsCtx = NULL;

    configASSERT( pxContext != NULL );

    pxLfsCtx = pxGetDefaultFsCtx();

    if( pxLfsCtx == NULL )
    {
        LogError( "File system is not ready" );
        xResult = pdFALSE;
    }
    else
    {
        lfs_file_t xFile = { 0 };
        lfs_ssize_t xLfsErr = LFS_ERR_CORRUPT;

        pxContext->xPalState = OTA_PAL_READY;
        pxContext->ulTargetBank = 0;
        pxContext->ulBaseAddress = 0;
        pxContext->ulImageSize = 0;

        /* Open the file */
        xLfsErr = lfs_file_open( pxLfsCtx, &xFile, IMAGE_CONTEXT_FILE_NAME, LFS_O_RDONLY );

        if( xLfsErr != LFS_ERR_OK )
        {
            LogInfo( "OTA PAL NV context file not found. Using defaults." );
        }
        else
        {
            OtaPalNvContext_t xNvContext = { 0 };

            xLfsErr = lfs_file_read( pxLfsCtx, &xFile, &xNvContext, sizeof( OtaPalNvContext_t ) );

            if( xLfsErr != sizeof( OtaPalNvContext_t ) )
            {
                LogError( " Failed to read OTA image context from file: %s, rc: %d", IMAGE_CONTEXT_FILE_NAME, xLfsErr );
            }
            else
            {
                pxContext->xPalState = xNvContext.xPalState;
                pxContext->ulTargetBank = xNvContext.ulFileTargetBank;
                pxContext->ulBaseAddress = 0;
                pxContext->ulImageSize = 0;
            }

            ( void ) lfs_file_close( pxLfsCtx, &xFile );
        }
    }

    return xResult;
}

static BaseType_t prvWritePalNvContext( OtaPalContext_t * pxContext )
{
    BaseType_t xResult = pdTRUE;
    lfs_t * pxLfsCtx = pxGetDefaultFsCtx();

    configASSERT( pxContext != NULL );

    if( pxLfsCtx == NULL )
    {
        xResult = pdFALSE;
        LogError( "File system not ready." );
    }
    else
    {
        lfs_ssize_t xLfsErr = LFS_ERR_CORRUPT;
        lfs_file_t xFile = { 0 };
        OtaPalNvContext_t xNvContext;

        xNvContext.ulFileTargetBank = pxContext->ulTargetBank;
        xNvContext.xPalState = pxContext->xPalState;

        /* Open the file */
        xLfsErr = lfs_file_open( pxLfsCtx, &xFile, IMAGE_CONTEXT_FILE_NAME, ( LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC ) );

        if( xLfsErr == LFS_ERR_OK )
        {
            xLfsErr = lfs_file_write( pxLfsCtx, &xFile, &xNvContext, sizeof( OtaPalNvContext_t ) );

            if( xLfsErr != sizeof( OtaPalNvContext_t ) )
            {
                LogError( "Failed to save OTA image context to file %s, error = %d.", IMAGE_CONTEXT_FILE_NAME, xLfsErr );
                xResult = pdFALSE;
            }

            ( void ) lfs_file_close( pxLfsCtx, &xFile );
        }
        else
        {
            LogError( "Failed to open file %s to save OTA image context, error = %d. ", IMAGE_CONTEXT_FILE_NAME, xLfsErr );
        }
    }

    return xResult;
}

static BaseType_t prvDeletePalNvContext( void )
{
    BaseType_t xResult = pdTRUE;
    lfs_t * pxLfsCtx = NULL;

    pxLfsCtx = pxGetDefaultFsCtx();

    if( pxLfsCtx == NULL )
    {
        xResult = pdFALSE;
        LogError( "File system not ready." );
    }
    else
    {
        struct lfs_info xFileInfo = { 0 };

        lfs_ssize_t xLfsErr = LFS_ERR_CORRUPT;

        xLfsErr = lfs_stat( pxLfsCtx, IMAGE_CONTEXT_FILE_NAME, &xFileInfo );

        if( xLfsErr == LFS_ERR_OK )
        {
            xLfsErr = lfs_remove( pxLfsCtx, IMAGE_CONTEXT_FILE_NAME );

            if( xLfsErr != LFS_ERR_OK )
            {
                xResult = pdFALSE;
            }
        }
    }

    return xResult;
}


static OtaPalContext_t * prvGetImageContext( void )
{
    OtaPalContext_t * pxCtx = NULL;

    if( xPalContext.xPalState == OTA_PAL_NOT_INITIALIZED )
    {
        if( prvInitializePalContext( &xPalContext ) == pdTRUE )
        {
            pxCtx = &xPalContext;
        }
    }
    else
    {
        pxCtx = &xPalContext;
    }

    return pxCtx;
}


static HAL_StatusTypeDef prvFlashSetDualBankMode( void )
{
    HAL_StatusTypeDef status = HAL_ERROR;
    FLASH_OBProgramInitTypeDef xObContext;

    /* Allow Access to Flash control registers and user Flash */
    status = HAL_FLASH_Unlock();

    if( status == HAL_OK )
    {
        /* Allow Access to option bytes sector */
        status = HAL_FLASH_OB_Unlock();

        if( status == HAL_OK )
        {
            /* Get the Dual bank configuration status */
            HAL_FLASHEx_OBGetConfig( &xObContext );

            if( ( xObContext.USERConfig & OB_DUALBANK_DUAL ) != OB_DUALBANK_DUAL )
            {
                xObContext.OptionType = OPTIONBYTE_USER;
                xObContext.USERType = OB_USER_DUALBANK;
                xObContext.USERConfig = OB_DUALBANK_DUAL;
                status = HAL_FLASHEx_OBProgram( &xObContext );
            }

            ( void ) HAL_FLASH_OB_Lock();
        }

        ( void ) HAL_FLASH_Lock();
    }

    return status;
}

static BaseType_t prvSelectBank( uint32_t ulNewBank )
{
    FLASH_OBProgramInitTypeDef xObContext = { 0 };
    BaseType_t xResult = pdTRUE;

    /* Validate selected bank */
    if( ( ulNewBank != FLASH_BANK_1 ) &&
        ( ulNewBank != FLASH_BANK_2 ) )
    {
        xResult = pdFALSE;
    }
    else if( prvGetBankSettingFromOB() != ulNewBank )
    {
        HAL_StatusTypeDef xHalStatus = HAL_ERROR;

        /* Allow Access to Flash control registers and user Flash */
        xHalStatus = HAL_FLASH_Unlock();

        if( xHalStatus != HAL_OK )
        {
            /* Lock on error */
            ( void ) HAL_FLASH_Lock();
        }
        else /* xHalStatus == HAL_OK */
        {
            /* Allow Access to option bytes sector */
            xHalStatus = HAL_FLASH_OB_Unlock();

            if( xHalStatus != HAL_OK )
            {
                /* Lock on error */
                ( void ) HAL_FLASH_Lock();
            }
        }

        if( xHalStatus == HAL_OK )
        {
            /* Get the SWAP_BANK status */
            HAL_FLASHEx_OBGetConfig( &xObContext );

            xObContext.OptionType = OPTIONBYTE_USER;
            xObContext.USERType = OB_USER_SWAP_BANK;

            if( ulNewBank == FLASH_BANK_2 )
            {
                xObContext.USERConfig = OB_SWAP_BANK_ENABLE;
            }
            else /* ulNewBank == FLASH_BANK_1  */
            {
                xObContext.USERConfig = OB_SWAP_BANK_DISABLE;
            }

            xHalStatus = HAL_FLASHEx_OBProgram( &xObContext );

            ( void ) HAL_FLASH_OB_Lock();
            ( void ) HAL_FLASH_Lock();
        }

        xResult = ( xHalStatus == HAL_OK ) ? pdTRUE : pdFALSE;
    }

    return xResult;
}

static void prvOptionByteApply( void )
{
    vPetWatchdog();

    if( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING )
    {
        vTaskSuspendAll();
    }

    LogSys( "Option Byte Launch in progress." );

    /* Drain log buffers */
    vDyingGasp();


    ( void ) HAL_FLASH_Unlock();
    ( void ) HAL_FLASH_OB_Unlock();
    /* Generate System Reset to load the new option byte values ***************/
    /* On successful option bytes loading the system should reset and control should not return from this function. */
    ( void ) HAL_FLASH_OB_Launch();
}

static uint32_t prvGetActiveBank( void )
{
    if( ulBankAtBootup == 0 )
    {
        ulBankAtBootup = prvGetBankSettingFromOB();
    }

    return ulBankAtBootup;
}

static uint32_t prvGetBankSettingFromOB( void )
{
    uint32_t ulCurrentBank = 0UL;
    FLASH_OBProgramInitTypeDef xObContext = { 0 };

    /* Get the Dual boot configuration status */
    HAL_FLASHEx_OBGetConfig( &xObContext );

    if( ( xObContext.USERConfig & OB_SWAP_BANK_ENABLE ) == OB_SWAP_BANK_ENABLE )
    {
        ulCurrentBank = FLASH_BANK_2;
    }
    else
    {
        ulCurrentBank = FLASH_BANK_1;
    }

    return ulCurrentBank;
}

static uint32_t prvGetInactiveBank( void )
{
    uint32_t ulActiveBank = prvGetActiveBank();

    uint32_t ulInactiveBank;

    if( ulActiveBank == FLASH_BANK_1 )
    {
        ulInactiveBank = FLASH_BANK_2;
    }
    else if( ulActiveBank == FLASH_BANK_2 )
    {
        ulInactiveBank = FLASH_BANK_1;
    }
    else
    {
        ulInactiveBank = 0UL;
    }

    return ulInactiveBank;
}


static HAL_StatusTypeDef prvWriteToFlash( uint32_t destination,
                                          uint8_t * pSource,
                                          uint32_t ulLength )
{
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t i = 0U;
    uint8_t quadWord[ 16 ] = { 0 };
    uint32_t numQuadWords = NUM_QUAD_WORDS( ulLength );
    uint32_t remainingBytes = NUM_REMAINING_BYTES( ulLength );

    /* Unlock the Flash to enable the flash control register access *************/
    HAL_FLASH_Unlock();

    for( i = 0U; i < numQuadWords; i++ )
    {
        /* Pet the watchdog */
        vPetWatchdog();

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

static BaseType_t prvEraseBank( uint32_t bankNumber )
{
    BaseType_t xResult = pdTRUE;

    configASSERT( ( bankNumber == FLASH_BANK_1 ) || ( bankNumber == FLASH_BANK_2 ) );

    configASSERT( bankNumber != prvGetActiveBank() );

    if( HAL_FLASH_Unlock() == HAL_OK )
    {
        uint32_t pageError = 0U;
        FLASH_EraseInitTypeDef pEraseInit;

        pEraseInit.Banks = bankNumber;
        pEraseInit.NbPages = FLASH_PAGE_NB;
        pEraseInit.Page = 0U;
        pEraseInit.TypeErase = FLASH_TYPEERASE_MASSERASE;

        if( HAL_FLASHEx_Erase( &pEraseInit, &pageError ) != HAL_OK )
        {
            LogError( "Failed to erase the flash bank, errorCode = %u, pageError = %u.", HAL_FLASH_GetError(), pageError );
            xResult = pdFALSE;
        }

        ( void ) HAL_FLASH_Lock();
    }
    else
    {
        LogError( "Failed to lock flash for erase, errorCode = %u.", HAL_FLASH_GetError() );
        xResult = pdFALSE;
    }

    return xResult;
}

static BaseType_t xCalculateImageHash( const unsigned char * pucImageAddress,
                                       const size_t uxImageLength,
                                       unsigned char * pucHashBuffer,
                                       size_t uxHashBufferLength,
                                       size_t * puxHashLength )
{
    BaseType_t xResult = pdTRUE;
    const mbedtls_md_info_t * pxMdInfo = NULL;

    pxMdInfo = mbedtls_md_info_from_type( MBEDTLS_MD_SHA256 );

    configASSERT( pucHashBuffer != NULL );
    configASSERT( pucImageAddress != NULL );
    configASSERT( uxImageLength > 0 );
    configASSERT( uxHashBufferLength > 0 );

    if( pxMdInfo == NULL )
    {
        LogError( "Failed to initialize mbedtls md_info object." );
        xResult = pdFALSE;
    }
    else if( mbedtls_md_get_size( pxMdInfo ) > uxHashBufferLength )
    {
        LogError( "Hash buffer is too small." );
        xResult = pdFALSE;
    }
    else
    {
        int lRslt = 0;
        size_t uxHashLength = 0;

        uxHashLength = mbedtls_md_get_size( pxMdInfo );

        configASSERT( uxHashLength <= MBEDTLS_MD_MAX_SIZE );

        lRslt = mbedtls_md( pxMdInfo, pucImageAddress, uxImageLength, pucHashBuffer );

        MBEDTLS_MSG_IF_ERROR( lRslt, "Failed to compute hash of the staged firmware image." );

        if( lRslt != 0 )
        {
            xResult = pdFALSE;
        }
        else
        {
            *puxHashLength = uxHashLength;
        }
    }

    return xResult;
}

static OtaPalStatus_t prvValidateSignature( const char * pcPubKeyLabel,
                                            const unsigned char * pucSignature,
                                            const size_t uxSignatureLength,
                                            const unsigned char * pucImageHash,
                                            const size_t uxHashLength )
{
    OtaPalStatus_t uxStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );

    PkiObject_t xOtaSigningPubKey;
    mbedtls_pk_context xPubKeyCtx;

    configASSERT( pcPubKeyLabel != NULL );
    configASSERT( pucImageHash != NULL );
    configASSERT( uxHashLength > 0 );

    if( ( pucSignature == NULL ) || ( uxSignatureLength <= 0 ) )
    {
        uxStatus = OTA_PAL_COMBINE_ERR( OtaPalBadSignerCert, 0 );
        return uxStatus;
    }

    mbedtls_pk_init( &xPubKeyCtx );

    xOtaSigningPubKey = xPkiObjectFromLabel( pcPubKeyLabel );

    if( xPkiReadPublicKey( &xPubKeyCtx, &xOtaSigningPubKey ) != PKI_SUCCESS )
    {
        LogError( "Failed to load OTA Signing public key." );
        uxStatus = OTA_PAL_COMBINE_ERR( OtaPalBadSignerCert, 0 );
    }

    /* Verify the provided signature against the image hash */
    if( OTA_PAL_MAIN_ERR( uxStatus ) == OtaPalSuccess )
    {
        int lRslt = mbedtls_pk_verify( &xPubKeyCtx, MBEDTLS_MD_SHA256,
                                       pucImageHash, uxHashLength,
                                       pucSignature, uxSignatureLength );

        if( lRslt != 0 )
        {
            MBEDTLS_MSG_IF_ERROR( lRslt, "OTA Image signature verification failed." );
            uxStatus = OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, lRslt );
        }
    }

    mbedtls_pk_free( &xPubKeyCtx );

    return uxStatus;
}

OtaPalStatus_t otaPal_CreateFileForRx( OtaFileContext_t * const pxFileContext )
{
    OtaPalStatus_t uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
    OtaPalContext_t * pxContext = prvGetImageContext();

    /* Handle back to back updates */
    if( ( pxContext->xPalState == OTA_PAL_ACCEPTED ) ||
        ( pxContext->xPalState == OTA_PAL_REJECTED ) )
    {
        pxContext->xPalState = OTA_PAL_READY;
    }

    LogInfo( "CreateFileForRx: xPalState: %s", pcPalStateToString( pxContext->xPalState ) );

    if( ( pxFileContext->fileSize > FLASH_BANK_SIZE ) ||
        ( pxFileContext->fileSize < OTA_IMAGE_MIN_SIZE ) )
    {
        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalRxFileTooLarge, 0 );
    }
    else if( strncmp( "b_u585i_iot02a_ntz.bin", ( char * ) pxFileContext->pFilePath, pxFileContext->filePathMaxSize ) != 0 )
    {
        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }
    else if( ( pxContext == NULL ) ||
             ( pxContext->xPalState != OTA_PAL_READY ) )
    {
        LogError( "OTA PAL context is NULL or not in the OTA_PAL_READY state." );
        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }
    else
    {
        uint32_t ulTargetBank = 0UL;

        /* Set dual bank mode if not already set. */
        if( prvFlashSetDualBankMode() != HAL_OK )
        {
            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
        }

        if( OTA_PAL_MAIN_ERR( uxOtaStatus ) == OtaPalSuccess )
        {
            ulTargetBank = prvGetInactiveBank();

            if( ulTargetBank == 0UL )
            {
                uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
            }
        }

        if( ( OTA_PAL_MAIN_ERR( uxOtaStatus ) == OtaPalSuccess ) &&
            ( prvEraseBank( ulTargetBank ) != pdTRUE ) )
        {
            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
        }

        if( OTA_PAL_MAIN_ERR( uxOtaStatus ) == OtaPalSuccess )
        {
            pxContext->ulTargetBank = ulTargetBank;
            pxContext->ulPendingBank = prvGetActiveBank();
            pxContext->ulBaseAddress = FLASH_START_INACTIVE_BANK;
            pxContext->ulImageSize = pxFileContext->fileSize;
            pxContext->xPalState = OTA_PAL_FILE_OPEN;
            pxFileContext->pFile = pxContext;
        }

        if( OTA_PAL_MAIN_ERR( uxOtaStatus ) == OtaPalSuccess )
        {
            if( prvDeletePalNvContext() == pdFALSE )
            {
                uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalBootInfoCreateFailed, 0 );
            }
        }
    }

    return uxOtaStatus;
}

int16_t otaPal_WriteBlock( OtaFileContext_t * const pxFileContext,
                           uint32_t offset,
                           uint8_t * const pData,
                           uint32_t blockSize )
{
    int16_t sBytesWritten = -1;
    OtaPalContext_t * pxContext = prvGetImageContext();

    configASSERT( pxContext->ulTargetBank != prvGetActiveBank() );

    configASSERT( blockSize < INT16_MAX );

    if( ( pxFileContext == NULL ) ||
        ( pxFileContext->pFile != ( uint8_t * ) ( pxContext ) ) )
    {
        LogError( "File context is invalid." );
    }
    else if( pxContext == NULL )
    {
        LogError( "PAL context is invalid." );
    }
    else if( ( offset + blockSize ) > pxContext->ulImageSize )
    {
        LogError( "Offset and blockSize exceeds image size" );
    }
    else if( pxContext->xPalState != OTA_PAL_FILE_OPEN )
    {
        LogError( "Invalid PAL State. otaPal_WriteBlock may only occur when file is open." );
    }
    else if( pData == NULL )
    {
        LogError( "pData is NULL." );
    }
    else if( prvWriteToFlash( ( pxContext->ulBaseAddress + offset ), pData, blockSize ) == HAL_OK )
    {
        sBytesWritten = ( int16_t ) blockSize;
    }

    return sBytesWritten;
}

OtaPalStatus_t otaPal_CloseFile( OtaFileContext_t * const pxFileContext )
{
    OtaPalStatus_t uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );

    OtaPalContext_t * pxContext = prvGetImageContext();

    configASSERT( pxFileContext );
    configASSERT( pxContext );
    configASSERT( pxFileContext->pSignature );

    if( ( pxFileContext->pFile == ( uint8_t * ) ( pxContext ) ) &&
        ( pxContext->xPalState == OTA_PAL_FILE_OPEN ) )

    {
        unsigned char pucHashBuffer[ MBEDTLS_MD_MAX_SIZE ];
        size_t uxHashLength = 0;

        if( xCalculateImageHash( ( unsigned char * ) ( pxContext->ulBaseAddress ),
                                 ( size_t ) pxContext->ulImageSize,
                                 pucHashBuffer, MBEDTLS_MD_MAX_SIZE, &uxHashLength ) != pdTRUE )
        {
            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalFileClose, 0 );
        }

        if( OTA_PAL_MAIN_ERR( uxOtaStatus ) == OtaPalSuccess )
        {
            uxOtaStatus = prvValidateSignature( ( char * ) pxFileContext->pCertFilepath,
                                                pxFileContext->pSignature->data,
                                                pxFileContext->pSignature->size,
                                                pucHashBuffer,
                                                uxHashLength );
        }

        if( OTA_PAL_MAIN_ERR( uxOtaStatus ) == OtaPalSuccess )
        {
            pxContext->xPalState = OTA_PAL_PENDING_ACTIVATION;
        }
        else
        {
            otaPal_SetPlatformImageState( pxFileContext, OtaImageStateRejected );
        }
    }
    else if( pxFileContext == NULL )
    {
        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalNullFileContext, 0 );
    }
    else
    {
        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalFileClose, 0 );
    }

    return uxOtaStatus;
}


OtaPalStatus_t otaPal_Abort( OtaFileContext_t * const pxFileContext )
{
    return otaPal_SetPlatformImageState( pxFileContext, OtaImageStateAborted );
}

OtaPalStatus_t otaPal_ActivateNewImage( OtaFileContext_t * const pxFileContext )
{
    OtaPalStatus_t uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, 0 );

    OtaPalContext_t * pxContext = prvGetImageContext();

    LogInfo( "ActivateNewImage xPalState: %s", pcPalStateToString( pxContext->xPalState ) );

    if( ( pxContext != NULL ) &&
        ( pxContext->xPalState == OTA_PAL_PENDING_ACTIVATION ) )
    {
        pxContext->ulPendingBank = pxContext->ulTargetBank;

        pxContext->xPalState = OTA_PAL_PENDING_SELF_TEST;
        uxOtaStatus = otaPal_ResetDevice( pxFileContext );
    }

    return uxOtaStatus;
}

void otaPal_EarlyInit( void )
{
    OtaPalContext_t * pxCtx = prvGetImageContext();

    ( void ) prvGetActiveBank();

    /* Check that context is valid */
    if( pxCtx != NULL )
    {
        LogSys( "OTA EarlyInit: State: %s, Current Bank: %d, Target Bank: %d.", pcPalStateToString( pxCtx->xPalState ), prvGetActiveBank(), pxCtx->ulTargetBank );

        switch( pxCtx->xPalState )
        {
            case OTA_PAL_PENDING_SELF_TEST:
                configASSERT( pxCtx->ulTargetBank == prvGetActiveBank() );

                /* Update the state to show that the new image was booted successfully */
                pxCtx->xPalState = OTA_PAL_NEW_IMAGE_BOOTED;
                ( void ) prvWritePalNvContext( pxCtx );
                break;

            case OTA_PAL_NEW_IMAGE_BOOTED:
                pxCtx->xPalState = OTA_PAL_NEW_IMAGE_WDT_RESET;
                pxCtx->ulPendingBank = ulGetOtherBank( pxCtx->ulTargetBank );
                ( void ) prvWritePalNvContext( pxCtx );

                LogError( "Detected a watchdog reset during first boot of new image. Reverting to bank: %d", ulGetOtherBank( pxCtx->ulTargetBank ) );
                ( void ) otaPal_ResetDevice( NULL );
                break;

            case OTA_PAL_NEW_IMAGE_WDT_RESET:
            case OTA_PAL_SELF_TEST_FAILED:
                pxCtx->xPalState = OTA_PAL_REJECTED;
                ( void ) prvDeletePalNvContext();
                ( void ) prvEraseBank( pxCtx->ulTargetBank );
                break;

            default:
                break;
        }

        LogSys( "OTA EarlyInit: Ending State: %s.", pcPalStateToString( pxCtx->xPalState ) );
    }
}

OtaPalStatus_t otaPal_SetPlatformImageState( OtaFileContext_t * const pxFileContext,
                                             OtaImageState_t xDesiredState )
{
    OtaPalStatus_t uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalBadImageState, 0 );

    OtaPalContext_t * pxContext = prvGetImageContext();

    LogInfo( "SetPlatformImageState: %s, %s", otaImageStateToString( xDesiredState ), pcPalStateToString( pxContext->xPalState ) );

    ( void ) pxFileContext;

    if( pxContext != NULL )
    {
        switch( xDesiredState )
        {
            case OtaImageStateAccepted:

                switch( pxContext->xPalState )
                {
                    case OTA_PAL_NEW_IMAGE_BOOTED:
                        configASSERT( prvGetActiveBank() == pxContext->ulTargetBank );
                        pxContext->ulPendingBank = pxContext->ulTargetBank;

                        /* Delete context from flash */
                        if( prvDeletePalNvContext() == pdTRUE )
                        {
                            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                            pxContext->xPalState = OTA_PAL_ACCEPTED;
                        }
                        else
                        {
                            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalCommitFailed, 0 );
                            LogError( "Failed to delete NV context file." );
                        }

                        break;

                    case OTA_PAL_ACCEPTED:
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                        break;

                    default:
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalCommitFailed, 0 );
                        break;
                }

                break;

            case OtaImageStateRejected:

                switch( pxContext->xPalState )
                {
                    /* Handle self-test or version check failure */
                    case OTA_PAL_PENDING_SELF_TEST:
                    case OTA_PAL_NEW_IMAGE_BOOTED:
                        pxContext->xPalState = OTA_PAL_SELF_TEST_FAILED;
                        pxContext->ulPendingBank = ulGetOtherBank( pxContext->ulTargetBank );
                        uxOtaStatus = otaPal_ResetDevice( pxFileContext );
                        break;

                    case OTA_PAL_SELF_TEST_FAILED:
                        configASSERT( prvGetActiveBank() == ulGetOtherBank( pxContext->ulTargetBank ) );

                        if( ( prvEraseBank( pxContext->ulTargetBank ) == pdTRUE ) &&
                            ( prvDeletePalNvContext() == pdTRUE ) )
                        {
                            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                            pxContext->xPalState = OTA_PAL_REJECTED;
                        }

                        break;

                    /* Handle failed verification */
                    case OTA_PAL_PENDING_ACTIVATION:
                    case OTA_PAL_FILE_OPEN:
                        pxContext->ulPendingBank = ulGetOtherBank( pxContext->ulTargetBank );

                        if( ( prvEraseBank( pxContext->ulTargetBank ) == pdTRUE ) &&
                            ( prvDeletePalNvContext() == pdTRUE ) )
                        {
                            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                            pxContext->xPalState = OTA_PAL_REJECTED;
                        }

                        break;

                    case OTA_PAL_NEW_IMAGE_WDT_RESET:
                    case OTA_PAL_REJECTED:
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                        break;

                    default:
                        break;
                }

                break;

            case OtaImageStateTesting:

                switch( pxContext->xPalState )
                {
                    case OTA_PAL_NEW_IMAGE_BOOTED:
                        configASSERT( prvGetActiveBank() == pxContext->ulTargetBank );
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                        break;

                    case OTA_PAL_PENDING_SELF_TEST:
                        configASSERT( prvGetActiveBank() == ulGetOtherBank( pxContext->ulTargetBank ) );
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalBadImageState, 0 );
                        break;

                    default:
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalBadImageState, 0 );
                        break;
                }

                break;

            case OtaImageStateAborted:

                switch( pxContext->xPalState )
                {
                    case OTA_PAL_READY:
                    case OTA_PAL_REJECTED:
                    case OTA_PAL_ACCEPTED:
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                        break;

                    case OTA_PAL_SELF_TEST_FAILED:
                        configASSERT( prvGetActiveBank() == ulGetOtherBank( pxContext->ulTargetBank ) );
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                        break;

                    case OTA_PAL_NEW_IMAGE_BOOTED:
                        configASSERT( prvGetActiveBank() == pxContext->ulTargetBank );
                        pxContext->ulPendingBank = ulGetOtherBank( pxContext->ulTargetBank );
                        uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                        break;

                    /* Handle abort and clear flash / nv context */
                    case OTA_PAL_FILE_OPEN:
                    case OTA_PAL_PENDING_ACTIVATION:
                    case OTA_PAL_PENDING_SELF_TEST:
                    case OTA_PAL_NEW_IMAGE_WDT_RESET:
                        configASSERT( prvGetActiveBank() == ulGetOtherBank( pxContext->ulTargetBank ) );

                        /* Clear any pending bank swap */
                        pxContext->ulPendingBank = prvGetActiveBank();

                        if( ( prvEraseBank( pxContext->ulTargetBank ) == pdTRUE ) &&
                            ( prvDeletePalNvContext() == pdTRUE ) )
                        {
                            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
                            pxContext->xPalState = OTA_PAL_READY;
                        }
                        else
                        {
                            LogError( "Failed to erase inactive bank." );
                            uxOtaStatus = OTA_PAL_COMBINE_ERR( OtaPalBadImageState, 0 );
                        }

                        break;

                    default:
                        break;
                }

                break;

            default:
                pxContext->xPalState = OTA_PAL_INVALID;
                break;
        }
    }

    return uxOtaStatus;
}


OtaPalImageState_t otaPal_GetPlatformImageState( OtaFileContext_t * const pxFileContext )
{
    OtaPalImageState_t xOtaState = OtaPalImageStateUnknown;
    OtaPalContext_t * pxContext = prvGetImageContext();

    ( void ) pxFileContext;

    configASSERT( pxFileContext );

    if( pxContext != NULL )
    {
        switch( pxContext->xPalState )
        {
            case OTA_PAL_NOT_INITIALIZED:
            case OTA_PAL_INVALID:
            default:
                xOtaState = OtaPalImageStateUnknown;
                break;

            case OTA_PAL_READY:
            case OTA_PAL_FILE_OPEN:
            case OTA_PAL_REJECTED:
            case OTA_PAL_NEW_IMAGE_WDT_RESET:
            case OTA_PAL_SELF_TEST_FAILED:
                xOtaState = OtaPalImageStateInvalid;
                break;

            case OTA_PAL_PENDING_ACTIVATION:
            case OTA_PAL_PENDING_SELF_TEST:
            case OTA_PAL_NEW_IMAGE_BOOTED:
                xOtaState = OtaPalImageStatePendingCommit;
                break;

            case OTA_PAL_ACCEPTED:
                xOtaState = OtaPalImageStateValid;
                break;
        }

        LogInfo( "GetPlatformImageState: %s: %s", otaPalImageStateToString( xOtaState ), pcPalStateToString( pxContext->xPalState ) );
    }

    return xOtaState;
}


OtaPalStatus_t otaPal_ResetDevice( OtaFileContext_t * const pxFileContext )
{
    OtaPalStatus_t uxStatus = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );

    ( void ) pxFileContext;
    OtaPalContext_t * pxContext = prvGetImageContext();

    LogSys( "OTA PAL reset request received. xPalState: %s", pcPalStateToString( pxContext->xPalState ) );

    if( pxContext != NULL )
    {
        /* Determine if context needs to be saved or deleted */
        switch( pxContext->xPalState )
        {
            /* A Reset during OTA_PAL_NEW_IMAGE_BOOTED means that the self test has failed */
            case OTA_PAL_NEW_IMAGE_BOOTED:
                configASSERT( prvGetActiveBank() == pxContext->ulTargetBank );


                pxContext->xPalState = OTA_PAL_SELF_TEST_FAILED;

                if( prvWritePalNvContext( pxContext ) != pdTRUE )
                {
                    LogError( "Failed to write to NV context." );
                    uxStatus = OTA_PAL_COMBINE_ERR( OtaPalRejectFailed, 0 );
                }

                pxContext->ulPendingBank = ulGetOtherBank( pxContext->ulTargetBank );
                break;

            /* Need to reset and start self test */
            case OTA_PAL_PENDING_SELF_TEST:
                configASSERT( prvGetActiveBank() == ulGetOtherBank( pxContext->ulTargetBank ) );

                pxContext->ulPendingBank = pxContext->ulTargetBank;

                if( prvWritePalNvContext( pxContext ) != pdTRUE )
                {
                    LogError( "Failed to write to NV context." );
                    uxStatus = OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, 0 );
                }

                break;

            case OTA_PAL_NEW_IMAGE_WDT_RESET:
                break;

            case OTA_PAL_SELF_TEST_FAILED:
                pxContext->ulPendingBank = ulGetOtherBank( pxContext->ulTargetBank );

                if( prvDeletePalNvContext() == pdFALSE )
                {
                    uxStatus = OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, 0 );
                }

                break;

            case OTA_PAL_READY:
            case OTA_PAL_FILE_OPEN:
            case OTA_PAL_PENDING_ACTIVATION:
                pxContext->ulPendingBank = ulGetOtherBank( pxContext->ulTargetBank );

            default:
            case OTA_PAL_NOT_INITIALIZED:
                pxContext->ulPendingBank = ulGetOtherBank( pxContext->ulTargetBank );
                break;
        }

        if( ( pxContext->ulPendingBank != 0 ) &&
            ( pxContext->ulPendingBank != prvGetActiveBank() ) )
        {
            LogSys( "Selecting Bank #%d.", pxContext->ulPendingBank );

            if( prvSelectBank( pxContext->ulPendingBank ) == pdTRUE )
            {
                prvOptionByteApply();
            }
            else
            {
                LogError( "Failed to select bank." );
                uxStatus = OTA_PAL_COMBINE_ERR( OtaPalUninitialized, 0 );
            }
        }
    }

    if( OTA_PAL_MAIN_ERR( uxStatus ) == OtaPalSuccess )
    {
        vDoSystemReset();

        LogError( "System reset failed" );

        uxStatus = OTA_PAL_COMBINE_ERR( OtaPalUninitialized, 0 );
    }

    return uxStatus;
}
