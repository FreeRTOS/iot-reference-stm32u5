/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 * @file mbedtls_pk_psa.c
 * @brief mbedtls_pk implementation for psa ECDSA keys.
 *           Exports a mbedtls_pk_info_t type.
 */

#include "tls_transport_config.h"

#ifdef MBEDTLS_TRANSPORT_PSA

#include <string.h>

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "psa/crypto.h"
#include "psa/crypto_types.h"
#include "psa/crypto_values.h"
#include "mbedtls_transport.h"

/* Mbedtls Includes */
#include "mbedtls/pk.h"
#include "mbedtls/asn1.h"
#include "mbedtls/platform.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/oid.h"
#include "pk_wrap.h"

#include "psa/internal_trusted_storage.h"
#include "psa/protected_storage.h"

#include "psa_util.h"

/* Forward declarations */
static int psa_ecdsa_check_pair( const void * pvPub,
                                 const void * pvPrv,
                                 int ( * lFRng )( void *, unsigned char *, size_t ),
                                 void * pvPRng );

static int psa_ecdsa_can_do( mbedtls_pk_type_t xType );

static size_t psa_ecdsa_get_bitlen( const void * pvCtx );

static int psa_ecdsa_sign( void * pvCtx,
                           mbedtls_md_type_t xMdAlg,
                           const unsigned char * pucHash,
                           size_t xHashLen,
                           unsigned char * pucSig,
                           size_t xSigBufferSize,
                           size_t * pxSigLen,
                           int ( * plRng )( void *, unsigned char *, size_t ),
                           void * pvRng );

static void * psa_pk_ctx_alloc( void );
static void psa_pk_ctx_free( void * pvCtx );
static void psa_ecdsa_debug( const void * pvCtx,
                             mbedtls_pk_debug_item * pxItems );

/* Type Definitions */

typedef struct PsaPkCtx_t
{
    mbedtls_ecdsa_context xEcdsaCtx;
    psa_key_id_t xKeyId;
} PsaPkCtx_t;


/* Globals */

const mbedtls_pk_info_t mbedtls_pk_psa_ecdsa =
{
    MBEDTLS_PK_ECKEY,
    "Opaque ECDSA",
    psa_ecdsa_get_bitlen,
    psa_ecdsa_can_do,
    NULL, /* verify_func */
    psa_ecdsa_sign,
#if defined( MBEDTLS_ECDSA_C ) && defined( MBEDTLS_ECP_RESTARTABLE )
    NULL, /* verify_rs_func */
    NULL, /* sign_rs_func */
#endif
    NULL, /* decrypt_func */
    NULL, /* encrypt_func */
    psa_ecdsa_check_pair,
    psa_pk_ctx_alloc,
    psa_pk_ctx_free,
#if defined( MBEDTLS_ECDSA_C ) && defined( MBEDTLS_ECP_RESTARTABLE )
    NULL, /* rs_alloc_func */
    NULL, /* rs_free_func */
#endif
    psa_ecdsa_debug,
};

/*-----------------------------------------------------------*/

psa_status_t xReadObjectFromPSACrypto( unsigned char ** ppucObject,
                                       size_t * puxObjectLen,
                                       psa_key_id_t xObjectId )
{
    psa_status_t xStatus = PSA_SUCCESS;
    psa_key_attributes_t xObjectAttrs = PSA_KEY_ATTRIBUTES_INIT;
    uint8_t * pucObjectBuffer = NULL;
    size_t uxBufferLen = 0;
    size_t uxObjectLen = 0;

    configASSERT( xObjectId >= PSA_KEY_ID_USER_MIN );
    configASSERT( xObjectId <= PSA_KEY_ID_VENDOR_MAX );

    if( ( ppucObject == NULL ) ||
        ( puxObjectLen == NULL ) )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        /* Fetch key attributes to validate the key id. */
        xStatus = psa_get_key_attributes( xObjectId, &xObjectAttrs );
    }

    /* Check that the key type is "raw" */
    if( ( xStatus == PSA_SUCCESS ) &&
        ( !PSA_KEY_TYPE_IS_UNSTRUCTURED( xObjectAttrs.type ) ) )
    {
        xStatus = PSA_ERROR_INVALID_HANDLE;
    }

    /* Determine length of buffer needed */
    if( xStatus == PSA_SUCCESS )
    {
        uxBufferLen = PSA_EXPORT_KEY_OUTPUT_SIZE( xObjectAttrs.type, xObjectAttrs.bits );
    }

    if( xStatus == PSA_SUCCESS )
    {
        pucObjectBuffer = mbedtls_calloc( 1, uxBufferLen );

        if( pucObjectBuffer == NULL )
        {
            xStatus = PSA_ERROR_INSUFFICIENT_MEMORY;
        }
    }

    if( xStatus == PSA_SUCCESS )
    {
        xStatus = psa_export_key( xObjectId,
                                  pucObjectBuffer,
                                  uxBufferLen,
                                  &uxObjectLen );
    }

    if( xStatus == PSA_SUCCESS )
    {
        configASSERT( pucObjectBuffer );
        configASSERT( uxObjectLen > 0 );
        *ppucObject = pucObjectBuffer;
        *puxObjectLen = uxObjectLen;
    }
    else
    {
        /* Free heap memory on error */
        if( pucObjectBuffer != NULL )
        {
            mbedtls_free( pucObjectBuffer );
            pucObjectBuffer = NULL;
        }
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

int32_t lWriteObjectToPsaIts( psa_storage_uid_t xObjectUid,
                              const uint8_t * pucData,
                              size_t uxDataLen )
{
    psa_status_t xStatus = PSA_SUCCESS;

    xStatus = psa_its_set( xObjectUid, uxDataLen, pucData, PSA_STORAGE_FLAG_NONE );

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lReadObjectFromPsaIts( uint8_t ** ppucData,
                               size_t * puxDataLen,
                               psa_storage_uid_t xObjectUid )
{
    psa_status_t xStatus = PSA_SUCCESS;
    struct psa_storage_info_t xStorageInfo = { 0 };
    void * pvDataBuffer = NULL;
    size_t uxDataLen = 0;

    configASSERT( xObjectUid > 0 );

    if( ppucData == NULL )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }

    if( xStatus == PSA_SUCCESS )
    {
        /* Fetch key attributes to validate the storage id. */
        xStatus = psa_its_get_info( xObjectUid, &xStorageInfo );
    }

    if( xStatus == PSA_SUCCESS )
    {
        pvDataBuffer = mbedtls_calloc( 1, xStorageInfo.size );

        if( pvDataBuffer == NULL )
        {
            xStatus = PSA_ERROR_INSUFFICIENT_MEMORY;
        }
    }

    if( xStatus == PSA_SUCCESS )
    {
        xStatus = psa_its_get( xObjectUid,
                               0,
                               xStorageInfo.size,
                               pvDataBuffer,
                               &uxDataLen );
    }

    if( xStatus == PSA_SUCCESS )
    {
        *ppucData = ( unsigned char * ) pvDataBuffer;

        if( puxDataLen != NULL )
        {
            *puxDataLen = uxDataLen;
        }
    }

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lWriteObjectToPsaPs( psa_storage_uid_t xObjectUid,
                             const uint8_t * pucData,
                             size_t uxDataLen )
{
    psa_status_t xStatus = PSA_SUCCESS;

    xStatus = psa_ps_set( xObjectUid, uxDataLen, pucData, PSA_STORAGE_FLAG_NONE );

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lReadObjectFromPsaPs( uint8_t ** ppucData,
                              size_t * puxDataLen,
                              psa_storage_uid_t xObjectUid )
{
    psa_status_t xStatus = PSA_SUCCESS;
    struct psa_storage_info_t xStorageInfo = { 0 };
    void * pvDataBuffer = NULL;
    size_t uxDataLen = 0;

    configASSERT( xObjectUid > 0 );

    if( ppucData == NULL )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }

    if( xStatus == PSA_SUCCESS )
    {
        /* Fetch key attributes to validate the storage id. */
        xStatus = psa_ps_get_info( xObjectUid, &xStorageInfo );
    }

    if( xStatus == PSA_SUCCESS )
    {
        pvDataBuffer = mbedtls_calloc( 1, xStorageInfo.size );

        if( pvDataBuffer == NULL )
        {
            xStatus = PSA_ERROR_INSUFFICIENT_MEMORY;
        }
    }

    if( xStatus == PSA_SUCCESS )
    {
        xStatus = psa_ps_get( xObjectUid,
                              0,
                              xStorageInfo.size,
                              pvDataBuffer,
                              &uxDataLen );
    }

    if( xStatus == PSA_SUCCESS )
    {
        *ppucData = ( unsigned char * ) pvDataBuffer;

        if( puxDataLen != NULL )
        {
            *puxDataLen = uxDataLen;
        }
    }

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lPsa_initMbedtlsPkContext( mbedtls_pk_context * pxMbedtlsPkCtx,
                                   psa_key_id_t xKeyId )
{
    PsaPkCtx_t * pxPsaPk = NULL;
    int lError = 0;

    if( ( pxMbedtlsPkCtx == NULL ) ||
        ( xKeyId == 0 ) )
    {
        lError = -1;
    }
    else
    {
        pxMbedtlsPkCtx->pk_ctx = psa_pk_ctx_alloc();

        if( pxMbedtlsPkCtx->pk_ctx == NULL )
        {
            lError = -1;
        }
        else
        {
            pxPsaPk = ( PsaPkCtx_t * ) pxMbedtlsPkCtx->pk_ctx;
            pxPsaPk->xKeyId = xKeyId;
        }
    }

    /* load public key */
    if( lError == 0 )
    {
        unsigned char * pucPubKeyDer = NULL;
        size_t uxPubKeyLen = 0;
        psa_status_t xPsaStatus = PSA_SUCCESS;

        xPsaStatus = xReadPublicKeyFromPSACrypto( &pucPubKeyDer, &uxPubKeyLen, pxPsaPk->xKeyId );

        if( xPsaStatus != PSA_SUCCESS )
        {
            lError = mbedtls_psa_err_translate_pk( xPsaStatus );
        }
        else
        {
            mbedtls_pk_context xPkContext = { 0 };

            mbedtls_pk_init( &xPkContext );

            lError = mbedtls_pk_parse_public_key( &xPkContext, pucPubKeyDer, uxPubKeyLen );

            if( lError == 0 )
            {
                configASSERT( xPkContext.pk_info->type == MBEDTLS_PK_ECKEY );

                if( xPkContext.pk_info->type == MBEDTLS_PK_ECKEY )
                {
                    mbedtls_ecp_keypair * pxPubKey = ( mbedtls_ecp_keypair * ) xPkContext.pk_ctx;

                    memcpy( &( pxPsaPk->xEcdsaCtx ), pxPubKey, sizeof( mbedtls_ecp_keypair ) );

                    /* Members of xPkContext.pk_ctx that are heap allocated transfer ownership to pxMbedtlsPkCtx */

                    mbedtls_free( xPkContext.pk_ctx );
                }
            }
        }

        if( pucPubKeyDer != NULL )
        {
            mbedtls_free( pucPubKeyDer );
            pucPubKeyDer = NULL;
        }
    }

    if( lError == 0 )
    {
        pxMbedtlsPkCtx->pk_info = &mbedtls_pk_psa_ecdsa;
    }
    else
    {
        psa_pk_ctx_free( pxMbedtlsPkCtx->pk_ctx );
    }

    return lError;
}

/*-----------------------------------------------------------*/

static void * psa_pk_ctx_alloc( void )
{
    void * pvCtx = NULL;

    pvCtx = mbedtls_calloc( 1, sizeof( PsaPkCtx_t ) );

    if( pvCtx != NULL )
    {
        PsaPkCtx_t * pxPsaPk = ( PsaPkCtx_t * ) pvCtx;

        pxPsaPk->xKeyId = 0;
        /* Initialize other fields */
        mbedtls_ecdsa_init( &( pxPsaPk->xEcdsaCtx ) );
    }

    return pvCtx;
}

/*-----------------------------------------------------------*/

static void psa_pk_ctx_free( void * pvCtx )
{
    if( pvCtx != NULL )
    {
        PsaPkCtx_t * pxPsaPk = ( PsaPkCtx_t * ) pvCtx;

        mbedtls_ecdsa_free( &( pxPsaPk->xEcdsaCtx ) );

        mbedtls_free( pvCtx );
    }
}



/*-----------------------------------------------------------*/

static int psa_ecdsa_sign( void * pvCtx,
                           mbedtls_md_type_t xMdAlg,
                           const unsigned char * pucHash,
                           size_t xHashLen,
                           unsigned char * pucSig,
                           size_t xSigBufferSize,
                           size_t * pxSigLen,
                           int ( * plRng )( void *, unsigned char *, size_t ),
                           void * pvRng )
{
    PsaPkCtx_t * pxPsaCtx = ( PsaPkCtx_t * ) pvCtx;

    psa_algorithm_t alg = PSA_ALG_ECDSA( mbedtls_psa_translate_md( xMdAlg ) );

    psa_status_t status = PSA_SUCCESS;

    /* Provided RNG is not used */
    ( void ) plRng;
    ( void ) pvRng;

    /* TODO: Determine why psa_sign_hash fails when large buffer sizes are provided as xSigBufferSize */

    if( xSigBufferSize >= PSA_SIGNATURE_MAX_SIZE )
    {
        status = psa_sign_hash( pxPsaCtx->xKeyId, alg, pucHash, xHashLen,
                                pucSig, PSA_SIGNATURE_MAX_SIZE, pxSigLen );
    }
    else
    {
        status = psa_sign_hash( pxPsaCtx->xKeyId, alg, pucHash, xHashLen,
                                pucSig, xSigBufferSize, pxSigLen );
    }

    if( status != PSA_SUCCESS )
    {
        return( mbedtls_psa_err_translate_pk( status ) );
    }

    /* Encode signature to an ASN.1 sequence */
    return( pk_ecdsa_sig_asn1_from_psa( pucSig, pxSigLen, xSigBufferSize ) );
}

/*-----------------------------------------------------------*/

static size_t psa_ecdsa_get_bitlen( const void * pvCtx )
{
    PsaPkCtx_t * pxPsaCtx = ( PsaPkCtx_t * ) pvCtx;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    size_t bits = 0;

    configASSERT( pvCtx );

    if( psa_get_key_attributes( pxPsaCtx->xKeyId, &attributes ) == PSA_SUCCESS )
    {
        bits = psa_get_key_bits( &attributes );
        psa_reset_key_attributes( &attributes );
    }

    return( bits );
}

/*-----------------------------------------------------------*/

static int psa_ecdsa_can_do( mbedtls_pk_type_t xType )
{
    return( xType == MBEDTLS_PK_ECDSA );
}

/*-----------------------------------------------------------*/

static int psa_ecdsa_check_pair( const void * pvPub,
                                 const void * pvPrv,
                                 int ( * lFRng )( void *, unsigned char *, size_t ),
                                 void * pvPRng )
{
    mbedtls_ecp_keypair xPubCtx;
    PsaPkCtx_t xPrvCtx;

    int lResult = 0;

    ( void ) lFRng;
    ( void ) pvPRng;

    if( ( pvPub == NULL ) || ( pvPrv == NULL ) )
    {
        lResult = -1;
    }
    else
    {
        memcpy( &xPubCtx, pvPub, sizeof( mbedtls_ecp_keypair ) );
        memcpy( &xPrvCtx, pvPrv, sizeof( PsaPkCtx_t ) );
    }

    if( lResult == 0 )
    {
        if( ( xPubCtx.grp.id == MBEDTLS_ECP_DP_NONE ) ||
            ( xPubCtx.grp.id != xPrvCtx.xEcdsaCtx.grp.id ) )
        {
            lResult = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
        }
    }

    /* Compare public key points */
    if( lResult == 0 )
    {
        lResult = mbedtls_mpi_cmp_mpi( &( xPubCtx.Q.X ), &( xPrvCtx.xEcdsaCtx.Q.X ) );
    }

    if( lResult == 0 )
    {
        lResult = mbedtls_mpi_cmp_mpi( &( xPubCtx.Q.Y ), &( xPrvCtx.xEcdsaCtx.Q.Y ) );
    }

    if( lResult == 0 )
    {
        lResult = mbedtls_mpi_cmp_mpi( &( xPubCtx.Q.Z ), &( xPrvCtx.xEcdsaCtx.Q.Z ) );
    }

    /* Test sign op */
    if( lResult == 0 )
    {
        unsigned char pucTestHash[ 32 ] =
        {
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
            0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
            0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
            0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38
        };
        unsigned char pucTestSignature[ MBEDTLS_ECDSA_MAX_SIG_LEN( 256 ) ] = { 0 };
        size_t uxSigLen = 0;

        lResult = psa_ecdsa_sign( &xPrvCtx, MBEDTLS_MD_SHA256,
                                  pucTestHash, sizeof( pucTestHash ),
                                  pucTestSignature, sizeof( pucTestSignature ), &uxSigLen,
                                  NULL, NULL );

        if( lResult == 0 )
        {
            lResult = mbedtls_ecdsa_read_signature( &xPubCtx,
                                                    pucTestHash, sizeof( pucTestHash ),
                                                    pucTestSignature, uxSigLen );
        }
    }

    return lResult;
}

/*-----------------------------------------------------------*/

static void psa_ecdsa_debug( const void * pvCtx,
                             mbedtls_pk_debug_item * pxItems )
{
    PsaPkCtx_t * pxPsaCtx = ( PsaPkCtx_t * ) pvCtx;

    configASSERT( mbedtls_ecdsa_info.debug_func );

    return mbedtls_ecdsa_info.debug_func( &( pxPsaCtx->xEcdsaCtx ), pxItems );
}

#endif /* MBEDTLS_TRANSPORT_PSA */
