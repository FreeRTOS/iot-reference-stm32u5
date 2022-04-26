/*
 * AWS IoT Over-the-air Update v3.0.0
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Copyright (c) 2021-2022 Arm Limited. All rights reserved.
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
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file ota_pal.c
 * @brief Platform Abstraction layer for AWS OTA based on PSA API
 *
 */

#include <string.h>


#include "logging_levels.h"
/* define LOG_LEVEL here if you want to modify the logging level from the default */

#define LOG_LEVEL    LOG_INFO

#include "logging.h"


#include "tls_transport_config.h"

/* OTA PAL Port include. */
#include "ota_pal.h"

/* PSA services. */
#include "psa/update.h"
#include "psa/crypto.h"

/***********************************************************************
 *
 * Macros
 *
 **********************************************************************/

#define ECDSA_SHA256_RAW_SIGNATURE_LENGTH     ( 64 )
/***********************************************************************
 *
 * Structures
 *
 **********************************************************************/

/***********************************************************************
 *
 * Variables
 *
 **********************************************************************/
 /**
 * @brief File Signature Key
 *
 * The OTA signature algorithm we support on this platform.
 */
const char OTA_JsonFileSignatureKey[ OTA_FILE_SIG_KEY_STR_MAX_LENGTH ] = "sig-sha256-ecdsa";

/**
 * @brief Ptr to system context
 *
 * Keep track of system context between calls from the OTA Agent
 *
 */
const OtaFileContext_t * pxSystemContext = NULL;
static psa_image_id_t xOTAImageID = TFM_FWU_INVALID_IMAGE_ID;

/* The key handle for OTA image verification. The key should be provisioned
 * before starting an OTA process by the user.
 */
static psa_key_handle_t xOTACodeVerifyKeyHandle = ( psa_key_handle_t ) OTA_SIGNING_KEY_ID;

static uint8_t ucECDSARAWSignature[ ECDSA_SHA256_RAW_SIGNATURE_LENGTH ] = { 0 };

/***********************************************************************
 *
 * Functions
 *
 **********************************************************************/

bool prvConvertToRawECDSASignature( const uint8_t * pucEncodedSignature,  uint8_t * pucRawSignature )
{
    bool xReturn = true;
    const uint8_t * pxNextLength = NULL;
    uint8_t ucSigComponentLength;

    if( ( pucRawSignature == NULL ) || ( pucEncodedSignature == NULL ) )
    {
        xReturn = false;
    }

    if( xReturn == true )
    {
        /* The signature has the format
         * SEQUENCE LENGTH (of entire rest of signature)
         *      INTEGER LENGTH  (of R component)
         *      INTEGER LENGTH  (of S component)
         */

        /* The 4th byte contains the length of the R component */
        ucSigComponentLength = pucEncodedSignature[ 3 ];

        /* The new signature will be 64 bytes long (32 bytes for R, 32 bytes for S).
         * Zero this buffer out in case a component is shorter than 32 bytes. */
        ( void ) memset( pucRawSignature, 0, ECDSA_SHA256_RAW_SIGNATURE_LENGTH );

        /********* R Component. *********/

        /* R components are represented by mbedTLS as 33 bytes when the first bit is zero to avoid any sign confusion. */
        if( ucSigComponentLength == 33UL )
        {
            /* Chop off the leading zero.  The first 4 bytes were SEQUENCE, LENGTH, INTEGER, LENGTH, 0x00 padding.  */
            ( void ) memcpy( pucRawSignature, &pucEncodedSignature[ 5 ], 32 );
            /* SEQUENCE, LENGTH, INTEGER, LENGTH, leading zero, R, S's integer tag */
            pxNextLength = &pucEncodedSignature[ 5U + 32U + 1U ];
        }
        else if( ucSigComponentLength <= 32UL )
        {
            /* The R component is 32 bytes or less.  Copy so that it is properly represented as a 32 byte value,
             * leaving leading 0 pads at beginning if necessary. */
            ( void ) memcpy( &pucRawSignature[ 32UL - ucSigComponentLength ],  /* If the R component is less than 32 bytes, leave the leading zeros. */
                             &pucEncodedSignature[ 4 ],                            /* SEQUENCE, LENGTH, INTEGER, LENGTH, (R component begins as the 5th byte) */
                             ucSigComponentLength );
            pxNextLength = &pucEncodedSignature[ 4U + ucSigComponentLength + 1U ]; /* Move the pointer to get rid of
                                                                                * SEQUENCE, LENGTH, INTEGER, LENGTH, R Component, S integer tag. */
        }
        else
        {
            xReturn = false;
        }

        /********** S Component. ***********/

        if( xReturn == true )
        {
            /* Now pxNextLength is pointing to the length of the S component. */
            ucSigComponentLength = pxNextLength[ 0 ];

            if( ucSigComponentLength == 33UL )
            {
                ( void ) memcpy( &pucRawSignature[ 32 ],
                                 &pxNextLength[ 2 ], /*LENGTH (of S component), 0x00 padding, S component is 3rd byte - we want to skip the leading zero. */
                                 32 );
            }
            else if( ucSigComponentLength <= 32UL )
            {
                /* The S component is 32 bytes or less.  Copy so that it is properly represented as a 32 byte value,
                 * leaving leading 0 pads at beginning if necessary. */
                ( void ) memcpy( &pucRawSignature[ 64UL - ucSigComponentLength ],
                                 &pxNextLength[ 1 ],
                                 ucSigComponentLength );
            }
            else
            {
                xReturn = false;
            }
        }
    }

    return xReturn;
}


static OtaPalStatus_t CalculatePSAImageID( uint8_t slot,
                                           OtaFileContext_t * const pFileContext,
                                           psa_image_id_t * pxImageID )
{
    uint32_t ulImageType = 0;

    if( pFileContext == NULL || pxImageID == NULL || pFileContext->pFilePath == NULL )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalUninitialized, 0 );
    }

    /* pFilePath field is got from the OTA server. */
    if( memcmp( pFileContext->pFilePath, "secure image", strlen("secure image") ) == 0 )
    {
        ulImageType = FWU_IMAGE_TYPE_SECURE;
    }
    else if( memcmp( pFileContext->pFilePath, "non_secure image", strlen("non_secure image") ) == 0 )
    {
        ulImageType = FWU_IMAGE_TYPE_NONSECURE;
    }
    else if( memcmp( pFileContext->pFilePath, "full image", strlen("full image") ) == 0 )
    {
        ulImageType = FWU_IMAGE_TYPE_FULL;
    }
    else
    {
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }

    *pxImageID = FWU_CALCULATE_IMAGE_ID(slot, ulImageType, ( uint16_t )pFileContext);

    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

/**
 * @brief Abort an OTA transfer.
 *
 * Aborts access to an existing open file represented by the OTA file context pFileContext. This is
 * only valid for jobs that started successfully.
 *
 * @note The input OtaFileContext_t pFileContext is checked for NULL by the OTA agent before this
 * function is called.
 *
 * This function may be called before the file is opened, so the file pointer pFileContext->fileHandle
 * may be NULL when this function is called.
 *
 * @param[in] pFileContext OTA file context information.
 *
 * @return The OtaPalStatus_t error code is a combination of the main OTA PAL interface error and
 *         the MCU specific sub error code. See ota_platform_interface.h for the OtaPalMainStatus_t
 *         error codes and your specific PAL implementation for the sub error code.
 *
 * Major error codes returned are:
 *
 *   OtaPalSuccess: Aborting access to the open file was successful.
 *   OtaPalFileAbort: Aborting access to the open file context was unsuccessful.
 */
OtaPalStatus_t otaPal_Abort( OtaFileContext_t * const pFileContext )
{
    if( (pFileContext == NULL) || ((pFileContext != pxSystemContext ) && ( pxSystemContext != NULL ) ) )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalAbortFailed, 0 );
    }

    if( pxSystemContext == NULL )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
    }

    if( psa_fwu_abort( xOTAImageID ) != PSA_SUCCESS )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalAbortFailed, 0 );
    }

    pxSystemContext = NULL;
    xOTAImageID = 0;

    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

/**
 * @brief Create a new receive file.
 *
 * @note Opens the file indicated in the OTA file context in the MCU file system.
 *
 * @note The previous image may be present in the designated image download partition or file, so the
 * partition or file must be completely erased or overwritten in this routine.
 *
 * @note The input OtaFileContext_t pFileContext is checked for NULL by the OTA agent before this
 * function is called.
 * The device file path is a required field in the OTA job document, so pFileContext->pFilePath is
 * checked for NULL by the OTA agent before this function is called.
 *
 * @param[in] pFileContext OTA file context information.
 *
 * @return The OtaPalStatus_t error code is a combination of the main OTA PAL interface error and
 *         the MCU specific sub error code. See ota_platform_interface.h for the OtaPalMainStatus_t
 *         error codes and your specific PAL implementation for the sub error code.
 *
 * Major error codes returned are:
 *
 *   OtaPalSuccess: File creation was successful.
 *   OtaPalRxFileTooLarge: The OTA receive file is too big for the platform to support.
 *   OtaPalBootInfoCreateFailed: The bootloader information file creation failed.
 *   OtaPalRxFileCreateFailed: Returned for other errors creating the file in the device's
 *                             non-volatile memory. If this error is returned, then the sub error
 *                             should be set to the appropriate platform specific value.
 */
OtaPalStatus_t otaPal_CreateFileForRx( OtaFileContext_t * const pFileContext )
{
    psa_image_id_t ulImageID = TFM_FWU_INVALID_IMAGE_ID;

    if( pFileContext == NULL || pFileContext->pFilePath == NULL )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }

    if( CalculatePSAImageID( FWU_IMAGE_ID_SLOT_STAGE, pFileContext, &ulImageID ) != OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 ) )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }

    pxSystemContext = pFileContext;
    xOTAImageID = ulImageID;
    pFileContext->pFile = &xOTAImageID;
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

static OtaPalStatus_t otaPal_CheckSignature( OtaFileContext_t * const pFileContext )
{
    psa_image_info_t xImageInfo = { 0 };
    psa_status_t uxStatus;
    psa_key_attributes_t xKeyAttribute = PSA_KEY_ATTRIBUTES_INIT;
    psa_algorithm_t xKeyAlgorithm = 0;
    bool xDecodeStatus;

    uxStatus = psa_fwu_query( xOTAImageID, &xImageInfo );
    if( uxStatus != PSA_SUCCESS )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, OTA_PAL_SUB_ERR( uxStatus ) );
    }

    if( prvConvertToRawECDSASignature( pFileContext->pSignature->data,  ucECDSARAWSignature ) == false )
    {
    	LogError( "Failed to decode ECDSA SHA256 signature." );
    	return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, 0 );
    }


    uxStatus = psa_get_key_attributes( xOTACodeVerifyKeyHandle, &xKeyAttribute );
    if( uxStatus != PSA_SUCCESS )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, OTA_PAL_SUB_ERR( uxStatus ) );
    }



    xKeyAlgorithm = psa_get_key_algorithm( &xKeyAttribute );
    uxStatus = psa_verify_hash( xOTACodeVerifyKeyHandle,
                                xKeyAlgorithm,
                                ( const uint8_t * )xImageInfo.digest,
                                ( size_t )PSA_FWU_MAX_DIGEST_SIZE,
								ucECDSARAWSignature,
								ECDSA_SHA256_RAW_SIGNATURE_LENGTH );
    if( uxStatus != PSA_SUCCESS )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, OTA_PAL_SUB_ERR( uxStatus ) );
    }

    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

/**
 * @brief Authenticate and close the underlying receive file in the specified OTA context.
 *
 * @note The input OtaFileContext_t pFileContext is checked for NULL by the OTA agent before this
 * function is called. This function is called only at the end of block ingestion.
 * otaPAL_CreateFileForRx() must succeed before this function is reached, so
 * pFileContext->fileHandle(or pFileContext->pFile) is never NULL.
 * The file signature key is required job document field in the OTA Agent, so pFileContext->pSignature will
 * never be NULL.
 *
 * If the signature verification fails, file close should still be attempted.
 *
 * @param[in] pFileContext OTA file context information.
 *
 * @return The OtaPalStatus_t error code is a combination of the main OTA PAL interface error and
 *         the MCU specific sub error code. See ota_platform_interface.h for the OtaPalMainStatus_t
 *         error codes and your specific PAL implementation for the sub error code.
 *
 * Major error codes returned are:
 *
 *   OtaPalSuccess on success.
 *   OtaPalSignatureCheckFailed: The signature check failed for the specified file.
 *   OtaPalBadSignerCert: The signer certificate was not readable or zero length.
 *   OtaPalFileClose: Error in low level file close.
 */
OtaPalStatus_t otaPal_CloseFile( OtaFileContext_t * const pFileContext )
{
    /* Check the signature. */
    return otaPal_CheckSignature( pFileContext );
}

/**
 * @brief Write a block of data to the specified file at the given offset.
 *
 * @note The input OtaFileContext_t pFileContext is checked for NULL by the OTA agent before this
 * function is called.
 * The file pointer/handle pFileContext->pFile, is checked for NULL by the OTA agent before this
 * function is called.
 * pData is checked for NULL by the OTA agent before this function is called.
 * blockSize is validated for range by the OTA agent before this function is called.
 * offset is validated by the OTA agent before this function is called.
 *
 * @param[in] pFileContext OTA file context information.
 * @param[in] ulOffset Byte offset to write to from the beginning of the file.
 * @param[in] pData Pointer to the byte array of data to write.
 * @param[in] ulBlockSize The number of bytes to write.
 *
 * @return The number of bytes written successfully, or a negative error code from the platform
 * abstraction layer.
 */
int16_t otaPal_WriteBlock( OtaFileContext_t * const pFileContext,
                           uint32_t ulOffset,
                           uint8_t * const pcData,
                           uint32_t ulBlockSize )
{
    uint32_t ulWriteLength, ulDoneLength = 0;

    if( (pFileContext == NULL) || (pFileContext != pxSystemContext ) || ( xOTAImageID == TFM_FWU_INVALID_IMAGE_ID ) )
    {
        return -1;
    }

    while (ulBlockSize > 0)
    {
        ulWriteLength = ulBlockSize <= PSA_FWU_MAX_BLOCK_SIZE ?
                        ulBlockSize : PSA_FWU_MAX_BLOCK_SIZE;
        /* Call the TF-M Firmware Update service to write image data. */
        if( psa_fwu_write( ( psa_image_id_t ) xOTAImageID,
                           ( size_t ) ulOffset + ulDoneLength,
                           ( const void * )(pcData + ulDoneLength),
                           ( size_t ) ulWriteLength ) != PSA_SUCCESS )
        {
            return -1;
        }
        ulBlockSize -= ulWriteLength;
        ulDoneLength += ulWriteLength;
    }

    return ulDoneLength;
}

/**
 * @brief Activate the newest MCU image received via OTA.
 *
 * This function shall take necessary actions to activate the newest MCU
 * firmware received via OTA. It is typically just a reset of the device.
 *
 * @note This function SHOULD NOT return. If it does, the platform does not support
 * an automatic reset or an error occurred.
 *
 * @param[in] pFileContext OTA file context information.
 *
 * @return The OtaPalStatus_t error code is a combination of the main OTA PAL interface error and
 *         the MCU specific sub error code. See ota_platform_interface.h for the OtaPalMainStatus_t
 *         error codes and your specific PAL implementation for the sub error code.
 *
 * Major error codes returned are:
 *
 *   OtaPalSuccess on success.
 *   OtaPalActivateFailed: The activation of the new OTA image failed.
 */
OtaPalStatus_t otaPal_ActivateNewImage( OtaFileContext_t * const pFileContext )
{
    psa_image_id_t xDependencyImageID;
    psa_image_version_t xDependencyVersion;
    psa_status_t uxStatus;

    if( (pFileContext == NULL) || (pFileContext != pxSystemContext ) || ( xOTAImageID == TFM_FWU_INVALID_IMAGE_ID ) )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, 0 );
    }

    uxStatus = psa_fwu_install( ( psa_image_id_t ) xOTAImageID, &xDependencyImageID, &xDependencyVersion );
    if( uxStatus == PSA_SUCCESS_REBOOT )
    {
        otaPal_ResetDevice( pFileContext );

        /* Reset failure happened. */
        return OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, 0 );
    }
    else if( uxStatus == PSA_SUCCESS )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
    }
    else
    {
        return OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, OTA_PAL_SUB_ERR( uxStatus ) );
    }
}

/**
 * @brief Attempt to set the state of the OTA update image.
 *
 * Take required actions on the platform to Accept/Reject the OTA update image (or bundle).
 * Refer to the PAL implementation to determine what happens on your platform.
 *
 * @param[in] pFileContext File context of type OtaFileContext_t.
 * @param[in] eState The desired state of the OTA update image.
 *
 * @return The OtaPalStatus_t error code is a combination of the main OTA PAL interface error and
 *         the MCU specific sub error code. See ota_platform_interface.h for the OtaPalMainStatus_t
 *         error codes and your specific PAL implementation for the sub error code.
 *
 * Major error codes returned are:
 *
 *   OtaPalSuccess on success.
 *   OtaPalBadImageState: if you specify an invalid OtaImageState_t. No sub error code.
 *   OtaPalAbortFailed: failed to roll back the update image as requested by OtaImageStateAborted.
 *   OtaPalRejectFailed: failed to roll back the update image as requested by OtaImageStateRejected.
 *   OtaPalCommitFailed: failed to make the update image permanent as requested by OtaImageStateAccepted.
 */
OtaPalStatus_t otaPal_SetPlatformImageState( OtaFileContext_t * const pFileContext,
                                             OtaImageState_t eState )
{
    psa_image_id_t ulImageID = TFM_FWU_INVALID_IMAGE_ID;
    psa_status_t uxStatus;

    if( pxSystemContext == NULL )
    {
        /* In this case, a reboot should have happened. */
        switch ( eState )
        {
            case OtaImageStateAccepted:
                if( CalculatePSAImageID( FWU_IMAGE_ID_SLOT_ACTIVE, pFileContext, &ulImageID ) != OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 ) )
                {
                    return OTA_PAL_COMBINE_ERR( OtaPalCommitFailed, 0 );
                }

                /* Make this image as a pernament one. */
                uxStatus = psa_fwu_accept( ulImageID );
                if( uxStatus != PSA_SUCCESS )
                {
                    return OTA_PAL_COMBINE_ERR( OtaPalCommitFailed, OTA_PAL_SUB_ERR( uxStatus ) );
                }
                break;
            case OtaImageStateRejected:
                /* The image is not the running image, the image in the secondary slot will be ereased if
                 * it is not a valid image. */
                break;
            case OtaImageStateTesting:
                break;
            case OtaImageStateAborted:
                /* The image download has been finished or has not been started.*/
                break;
            default:
                return OTA_PAL_COMBINE_ERR( OtaPalBadImageState, 0 );
        }
    }
    else
    {
        if( eState == OtaImageStateAccepted )
        {
            /* The image can only be set as accepted after a reboot. So the pxSystemContext should be NULL. */
            return OTA_PAL_COMBINE_ERR( OtaPalCommitFailed, 0 );
        }

        /* The image is still downloading and the OTA process will not continue. The image is in
         * the secondary slot and does not impact the later update process. So nothing to do in
         * other state.
         */
    }

    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

/**
 * @brief Get the state of the OTA update image.
 *
 * We read this at OTA_Init time and when the latest OTA job reports itself in self
 * test. If the update image is in the "pending commit" state, we start a self test
 * timer to assure that we can successfully connect to the OTA services and accept
 * the OTA update image within a reasonable amount of time (user configurable). If
 * we don't satisfy that requirement, we assume there is something wrong with the
 * firmware and automatically reset the device, causing it to roll back to the
 * previously known working code.
 *
 * If the update image state is not in "pending commit," the self test timer is
 * not started.
 *
 * @param[in] pFileContext File context of type OtaFileContext_t.
 *
 * @return An OtaPalImageState_t. One of the following:
 *   OtaPalImageStatePendingCommit (the new firmware image is in the self test phase)
 *   OtaPalImageStateValid         (the new firmware image is already committed)
 *   OtaPalImageStateInvalid       (the new firmware image is invalid or non-existent)
 *
 *   NOTE: OtaPalImageStateUnknown should NEVER be returned and indicates an implementation error.
 */
OtaPalImageState_t otaPal_GetPlatformImageState( OtaFileContext_t * const pFileContext )
{
    psa_status_t uxStatus;
    psa_image_info_t xImageInfo = { 0 };
    psa_image_id_t ulImageID = TFM_FWU_INVALID_IMAGE_ID;
    uint8_t ucSlot;

    if( pxSystemContext == NULL )
    {
        ucSlot = FWU_IMAGE_ID_SLOT_ACTIVE;

    }
    else
    {
        ucSlot = FWU_IMAGE_ID_SLOT_STAGE;
    }

    if( CalculatePSAImageID( ucSlot, pFileContext, &ulImageID ) != OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 ) )
    {
        return OtaPalImageStateInvalid;
    }

    uxStatus = psa_fwu_query( ulImageID, &xImageInfo );
    if( uxStatus != PSA_SUCCESS )
    {
        return OtaPalImageStateInvalid;
    }

    switch ( xImageInfo.state )
    {
        case PSA_IMAGE_PENDING_INSTALL:
            return OtaPalImageStatePendingCommit;
        case PSA_IMAGE_INSTALLED:
            return OtaPalImageStateValid;
        default:
            return OtaPalImageStateInvalid;
    }

    /* It should never goes here. But just for coding safety. */
    return OtaPalImageStateInvalid;
}

/**
 * @brief Reset the device.
 *
 * This function shall reset the MCU and cause a reboot of the system.
 *
 * @note This function SHOULD NOT return. If it does, the platform does not support
 * an automatic reset or an error occurred.
 *
 * @param[in] pFileContext OTA file context information.
 *
 * @return The OtaPalStatus_t error code is a combination of the main OTA PAL interface error and
 *         the MCU specific sub error code. See ota_platform_interface.h for the OtaPalMainStatus_t
 *         error codes and your specific PAL implementation for the sub error code.
 */
OtaPalStatus_t otaPal_ResetDevice( OtaFileContext_t * const pFileContext )
{
    psa_fwu_request_reboot();
    return OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, 0 );
}
