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

#include "tls_transport_config.h"

#ifdef MBEDTLS_TRANSPORT_PKCS11

/**
 * @file mbedtls_pk_pkcs11.c
 * @brief mbedtls_pk implementation for pkcs11 ECDSA and RSA keys.
 *           Exports a mbedtls_pk_info_t type.
 */

#include <string.h>

/* Mbedtls Includes */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/private_access.h"
#include "mbedtls/pk.h"
#include "mbedtls/asn1.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform.h"
#include "mbedtls/asn1write.h"
#include "mbedtls/ecdsa.h"
#include "pk_wrap.h"

#include "core_pkcs11_config.h"
#include "core_pkcs11.h"


typedef struct P11PkCtx
{
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_SESSION_HANDLE xSessionHandle;
    CK_OBJECT_HANDLE xPkHandle;
} P11PkCtx_t;

typedef struct P11EcDsaCtx
{
    mbedtls_ecdsa_context xMbedEcDsaCtx;
    P11PkCtx_t xP11PkCtx;
} P11EcDsaCtx_t;

typedef struct P11RsaCtx
{
    mbedtls_rsa_context xMbedRsaCtx;
    P11PkCtx_t xP11PkCtx;
} P11RsaCtx_t;

static void * p11_ecdsa_ctx_alloc( void );

static CK_RV p11_ecdsa_ctx_init( void * pvCtx,
                                 CK_FUNCTION_LIST_PTR pxFunctionList,
                                 CK_SESSION_HANDLE xSessionHandle,
                                 CK_OBJECT_HANDLE xPkHandle );

static void p11_ecdsa_ctx_free( void * pvCtx );

static int p11_ecdsa_sign( void * pvCtx,
                           mbedtls_md_type_t xMdAlg,
                           const unsigned char * pucHash,
                           size_t xHashLen,
                           unsigned char * pucSig,
                           size_t xSigBufferSize,
                           size_t * pxSigLen,
                           int ( * plRng )( void *, unsigned char *, size_t ),
                           void * pvRng );

static size_t p11_ecdsa_get_bitlen( const void * pvCtx );

static int p11_ecdsa_can_do( mbedtls_pk_type_t xType );

static int p11_ecdsa_verify( void * pvCtx,
                             mbedtls_md_type_t xMdAlg,
                             const unsigned char * pucHash,
                             size_t xHashLen,
                             const unsigned char * pucSig,
                             size_t xSigLen );

static int p11_ecdsa_check_pair( const void * pvPub,
                                 const void * pvPrv,
                                 int ( * lFRng )( void *, unsigned char *, size_t ),
                                 void * pvPRng );

static void p11_ecdsa_debug( const void * pvCtx,
                             mbedtls_pk_debug_item * pxItems );

/* Helper functions */
static int prvEcdsaSigToASN1InPlace( unsigned char * pucSig,
                                     size_t xSigBufferSize,
                                     size_t * pxSigLen );

static int prvASN1WriteBigIntFromOctetStr( unsigned char ** ppucPosition,
                                           const unsigned char * pucStart,
                                           const unsigned char * pucOctetStr,
                                           size_t xOctetStrLen );

mbedtls_pk_info_t mbedtls_pkcs11_pk_ecdsa =
{
    .type            = MBEDTLS_PK_ECKEY,
    .name            = "PKCS#11",
    .get_bitlen      = p11_ecdsa_get_bitlen,
    .can_do          = p11_ecdsa_can_do,
    .verify_func     = p11_ecdsa_verify,
    .sign_func       = p11_ecdsa_sign,
#if defined( MBEDTLS_ECDSA_C ) && defined( MBEDTLS_ECP_RESTARTABLE )
    .verify_rs_func  = NULL,
    .sign_rs_func    = NULL,
#endif /* MBEDTLS_ECDSA_C && MBEDTLS_ECP_RESTARTABLE */
    .decrypt_func    = NULL,
    .encrypt_func    = NULL,
    .check_pair_func = p11_ecdsa_check_pair,
    .ctx_alloc_func  = p11_ecdsa_ctx_alloc,
    .ctx_free_func   = p11_ecdsa_ctx_free,
#if defined( MBEDTLS_ECDSA_C ) && defined( MBEDTLS_ECP_RESTARTABLE )
    .rs_alloc_func   = NULL,
    .rs_free_func    = NULL,
#endif /* MBEDTLS_ECDSA_C && MBEDTLS_ECP_RESTARTABLE */
    .debug_func      = p11_ecdsa_debug,
};


static size_t p11_rsa_get_bitlen( const void * pvCtx );

static int p11_rsa_can_do( mbedtls_pk_type_t xType );

static int p11_rsa_verify( void * pvCtx,
                           mbedtls_md_type_t xMdAlg,
                           const unsigned char * pucHash,
                           size_t xHashLen,
                           const unsigned char * pucSig,
                           size_t xSigLen );

static int p11_rsa_sign( void * ctx,
                         mbedtls_md_type_t md_alg,
                         const unsigned char * hash,
                         size_t hash_len,
                         unsigned char * sig,
                         size_t sig_size,
                         size_t * sig_len,
                         int ( * f_rng )( void *, unsigned char *, size_t ),
                         void * p_rng );

static int p11_rsa_check_pair( const void * pvPub,
                               const void * pvPrv,
                               int ( * lFRng )( void *, unsigned char *, size_t ),
                               void * pvPRng );

static void * p11_rsa_ctx_alloc( void );

static CK_RV p11_rsa_ctx_init( void * pvCtx,
                               CK_FUNCTION_LIST_PTR pxFunctionList,
                               CK_SESSION_HANDLE xSessionHandle,
                               CK_OBJECT_HANDLE xPkHandle );

static void p11_rsa_ctx_free( void * pvCtx );

static void p11_rsa_debug( const void * pvCtx,
                           mbedtls_pk_debug_item * pxItems );

mbedtls_pk_info_t mbedtls_pkcs11_pk_rsa =
{
    .type            = MBEDTLS_PK_RSA,
    .name            = "PKCS#11",
    .get_bitlen      = p11_rsa_get_bitlen,
    .can_do          = p11_rsa_can_do,
    .verify_func     = p11_rsa_verify,
    .sign_func       = p11_rsa_sign,
#if defined( MBEDTLS_ECDSA_C ) && defined( MBEDTLS_ECP_RESTARTABLE )
    .verify_rs_func  = NULL,
    .sign_rs_func    = NULL,
#endif /* MBEDTLS_ECDSA_C && MBEDTLS_ECP_RESTARTABLE */
    .decrypt_func    = NULL,
    .encrypt_func    = NULL,
    .check_pair_func = p11_rsa_check_pair,
    .ctx_alloc_func  = p11_rsa_ctx_alloc,
    .ctx_free_func   = p11_rsa_ctx_free,
#if defined( MBEDTLS_ECDSA_C ) && defined( MBEDTLS_ECP_RESTARTABLE )
    .rs_alloc_func   = NULL,
    .rs_free_func    = NULL,
#endif /* MBEDTLS_ECDSA_C && MBEDTLS_ECP_RESTARTABLE */
    .debug_func      = p11_rsa_debug,
};

/*-----------------------------------------------------------*/

int32_t lReadCertificateFromPKCS11( mbedtls_x509_crt * pxCertificateContext,
                                    CK_SESSION_HANDLE xP11SessionHandle,
                                    const char * pcCertificateLabel,
                                    size_t xLabelLen )
{
    CK_RV xResult = CKR_OK;
    int32_t lResult = 0;
    CK_ATTRIBUTE xTemplate = { 0 };
    CK_OBJECT_HANDLE xCertObj = 0;
    char pcCertLabelCopy[ pkcs11configMAX_LABEL_LENGTH ] = { 0 };
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;

    configASSERT( xP11SessionHandle != CK_INVALID_HANDLE );
    configASSERT( pcCertificateLabel != NULL );
    configASSERT( pxCertificateContext );

    ( void ) strncpy( pcCertLabelCopy, pcCertificateLabel, xLabelLen );

    if( C_GetFunctionList( &pxFunctionList ) != CKR_OK )
    {
        lResult = CKR_FUNCTION_FAILED;
    }

    if( xResult == CKR_OK )
    {
        /* Get the handle of the certificate. */
        xResult = xFindObjectWithLabelAndClass( xP11SessionHandle,
                                                pcCertLabelCopy,
                                                xLabelLen,
                                                CKO_CERTIFICATE,
                                                &xCertObj );

        if( xCertObj == CK_INVALID_HANDLE )
        {
            lResult = CKR_OBJECT_HANDLE_INVALID;
        }
    }

    /* Query the certificate size. */
    if( CKR_OK == xResult )
    {
        xTemplate.type = CKA_VALUE;
        xTemplate.ulValueLen = 0;
        xTemplate.pValue = NULL;

        xResult = pxFunctionList->C_GetAttributeValue( xP11SessionHandle,
                                                       xCertObj,
                                                       &xTemplate,
                                                       1 );
    }

    /* Create a buffer for the certificate. */
    if( CKR_OK == xResult )
    {
        xTemplate.pValue = mbedtls_calloc( 1, xTemplate.ulValueLen );

        if( NULL == xTemplate.pValue )
        {
            xResult = CKR_HOST_MEMORY;
        }
    }

    /* Export the certificate. */
    if( CKR_OK == xResult )
    {
        xResult = pxFunctionList->C_GetAttributeValue( xP11SessionHandle,
                                                       xCertObj,
                                                       &xTemplate,
                                                       1 );
    }

    /* Decode the certificate. */
    if( CKR_OK == xResult )
    {
        lResult = mbedtls_x509_crt_parse( pxCertificateContext,
                                          ( unsigned char * ) xTemplate.pValue,
                                          xTemplate.ulValueLen );
    }
    else
    {
        lResult = ( int32_t ) xResult;
    }

    /* Free memory. */
    if( xTemplate.pValue != NULL )
    {
        mbedtls_free( xTemplate.pValue );
    }

    return lResult;
}

/*-----------------------------------------------------------*/

int32_t lWriteCertificateToPKCS11( const mbedtls_x509_crt * pxCertificateContext,
                                   CK_SESSION_HANDLE xP11SessionHandle,
                                   char * pcCertificateLabel,
                                   size_t uxCertificateLabelLen )
{
    CK_RV xResult;
    CK_OBJECT_HANDLE xCertHandle = 0;
    CK_FUNCTION_LIST_PTR pxFunctionList;

    configASSERT( pxCertificateContext );
    configASSERT( pxCertificateContext->raw.p );
    configASSERT( pxCertificateContext->raw.len > 0 );
    configASSERT( xP11SessionHandle );
    configASSERT( pcCertificateLabel );
    configASSERT( uxCertificateLabelLen > 0 );

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        /* Look for an existing object that we may need to overwrite */
        xResult = xFindObjectWithLabelAndClass( xP11SessionHandle,
                                                pcCertificateLabel,
                                                uxCertificateLabelLen,
                                                CKO_CERTIFICATE,
                                                &xCertHandle );
    }

    if( ( xResult == CKR_OK ) &&
        ( xCertHandle != CK_INVALID_HANDLE ) )
    {
        xResult = pxFunctionList->C_DestroyObject( xP11SessionHandle,
                                                   xCertHandle );
    }

    if( xResult == CKR_OK )
    {
        CK_OBJECT_CLASS xObjClass = CKO_CERTIFICATE;
        CK_CERTIFICATE_TYPE xCertType = CKC_X_509;
        CK_BBOOL xPersistCert = CK_TRUE;


        CK_ATTRIBUTE pxTemplate[ 5 ] =
        {
            {
                .type = CKA_CLASS,
                .ulValueLen = sizeof( CK_OBJECT_CLASS ),
                .pValue = &xObjClass,
            },
            {
                .type = CKA_LABEL,
                .ulValueLen = uxCertificateLabelLen,
                .pValue = pcCertificateLabel,
            },
            {
                .type = CKA_CERTIFICATE_TYPE,
                .ulValueLen = sizeof( CK_CERTIFICATE_TYPE ),
                .pValue = &xCertType,
            },
            {
                .type = CKA_TOKEN,
                .ulValueLen = sizeof( CK_BBOOL ),
                .pValue = &xPersistCert,
            },
            {
                .type = CKA_VALUE,
                .ulValueLen = pxCertificateContext->raw.len,
                .pValue = pxCertificateContext->raw.p,
            }
        };

        xResult = pxFunctionList->C_CreateObject( xP11SessionHandle,
                                                  pxTemplate,
                                                  5,
                                                  &xCertHandle );
    }

    return( ( xResult == CKR_OK ) ? 0 : -1 );
}

/*-----------------------------------------------------------*/

#define EC_POINT_LEN    256

int32_t lWriteEcPublicKeyToPKCS11( const mbedtls_pk_context * pxPubKeyContext,
                                   CK_SESSION_HANDLE xP11SessionHandle,
                                   char * pcPubKeyLabel,
                                   size_t uxPubKeyLabelLen )
{
    CK_RV xResult;
    CK_OBJECT_HANDLE xCertHandle = 0;
    CK_FUNCTION_LIST_PTR pxFunctionList;
    size_t uxEcPointLength = 0;
    CK_BYTE * pucEcPoint = NULL;
    static CK_BYTE pucEcParams[] = pkcs11DER_ENCODED_OID_P256;

    configASSERT( pxPubKeyContext );
    configASSERT( pxPubKeyContext->pk_ctx );
    configASSERT( pxPubKeyContext->pk_info );
    configASSERT( xP11SessionHandle );
    configASSERT( pcPubKeyLabel );
    configASSERT( uxPubKeyLabelLen > 0 );

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        /* Look for an existing object that we may need to overwrite */
        xResult = xFindObjectWithLabelAndClass( xP11SessionHandle,
                                                pcPubKeyLabel,
                                                uxPubKeyLabelLen,
                                                CKO_PUBLIC_KEY,
                                                &xCertHandle );
    }

    if( ( xResult == CKR_OK ) &&
        ( xCertHandle != CK_INVALID_HANDLE ) )
    {
        xResult = pxFunctionList->C_DestroyObject( xP11SessionHandle,
                                                   xCertHandle );
    }

    /* Export EC Point from mbedtls context to binary form */
    if( xResult == CKR_OK )
    {
        if( pxPubKeyContext->pk_info->type == MBEDTLS_PK_ECKEY )
        {
            int lRslt = 0;

            mbedtls_ecp_keypair * pxEcpKey = ( mbedtls_ecp_keypair * ) ( pxPubKeyContext->pk_ctx );

            /* Determine length */
            lRslt = mbedtls_ecp_point_write_binary( &( pxEcpKey->grp ),
                                                    &( pxEcpKey->Q ),
                                                    MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                    &uxEcPointLength,
                                                    NULL,
                                                    0 );

            if( ( lRslt == MBEDTLS_ERR_ECP_BUFFER_TOO_SMALL ) ||
                ( lRslt == 0 ) )
            {
                pucEcPoint = mbedtls_calloc( 1, uxEcPointLength + 2 );

                if( pucEcPoint == NULL )
                {
                    xResult = CKR_HOST_MEMORY;
                }
            }
            else
            {
                xResult = CKR_FUNCTION_FAILED;
            }

            if( xResult == CKR_OK )
            {
                if( uxEcPointLength <= UINT8_MAX )
                {
                    pucEcPoint[ 0 ] = MBEDTLS_ASN1_OCTET_STRING;
                    pucEcPoint[ 1 ] = ( CK_BYTE ) uxEcPointLength;
                }
                else
                {
                    xResult = CKR_FUNCTION_FAILED;
                }
            }

            if( xResult == CKR_OK )
            {
                /* Write public key */
                lRslt = mbedtls_ecp_point_write_binary( &( pxEcpKey->grp ),
                                                        &( pxEcpKey->Q ),
                                                        MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                        &uxEcPointLength,
                                                        &( pucEcPoint[ 2 ] ),
                                                        uxEcPointLength );

                if( lRslt != 0 )
                {
                    xResult = CKR_FUNCTION_FAILED;
                }
                else
                {
                    uxEcPointLength += 2;
                }
            }
        }
        else
        {
            xResult = CKR_FUNCTION_NOT_SUPPORTED;
        }
    }

    if( xResult == CKR_OK )
    {
        CK_OBJECT_CLASS xObjClass = CKO_PUBLIC_KEY;
        CK_KEY_TYPE xKeyType = CKK_EC;
        CK_BBOOL xPersistKey = CK_TRUE;
        CK_BBOOL xVerify = CK_TRUE;

        CK_ATTRIBUTE pxTemplate[ 7 ] =
        {
            {
                .type = CKA_CLASS,
                .ulValueLen = sizeof( CK_OBJECT_CLASS ),
                .pValue = &xObjClass,
            },
            {
                .type = CKA_KEY_TYPE,
                .ulValueLen = sizeof( CK_KEY_TYPE ),
                .pValue = &xKeyType,
            },
            {
                .type = CKA_LABEL,
                .ulValueLen = uxPubKeyLabelLen,
                .pValue = pcPubKeyLabel,
            },
            {
                .type = CKA_TOKEN,
                .ulValueLen = sizeof( CK_BBOOL ),
                .pValue = &xPersistKey,
            },
            {
                .type = CKA_EC_PARAMS,
                .ulValueLen = sizeof( pucEcParams ),
                .pValue = pucEcParams,
            },
            {
                .type = CKA_VERIFY,
                .ulValueLen = sizeof( CK_BBOOL ),
                .pValue = &xVerify,
            },
            {
                .type = CKA_EC_POINT,
                .ulValueLen = uxEcPointLength,
                .pValue = pucEcPoint,
            }
        };

        xResult = pxFunctionList->C_CreateObject( xP11SessionHandle,
                                                  pxTemplate,
                                                  7,
                                                  &xCertHandle );
    }

    if( pucEcPoint != NULL )
    {
        mbedtls_free( pucEcPoint );
        pucEcPoint = NULL;
    }

    return( ( xResult == CKR_OK ) ? 0 : -1 );
}

/*-----------------------------------------------------------*/

const char * pcPKCS11StrError( CK_RV xError )
{
    switch( xError )
    {
        case CKR_OK:
            return "CKR_OK";

        case CKR_CANCEL:
            return "CKR_CANCEL";

        case CKR_HOST_MEMORY:
            return "CKR_HOST_MEMORY";

        case CKR_SLOT_ID_INVALID:
            return "CKR_SLOT_ID_INVALID";

        case CKR_GENERAL_ERROR:
            return "CKR_GENERAL_ERROR";

        case CKR_FUNCTION_FAILED:
            return "CKR_FUNCTION_FAILED";

        case CKR_ARGUMENTS_BAD:
            return "CKR_ARGUMENTS_BAD";

        case CKR_NO_EVENT:
            return "CKR_NO_EVENT";

        case CKR_NEED_TO_CREATE_THREADS:
            return "CKR_NEED_TO_CREATE_THREADS";

        case CKR_CANT_LOCK:
            return "CKR_CANT_LOCK";

        case CKR_ATTRIBUTE_READ_ONLY:
            return "CKR_ATTRIBUTE_READ_ONLY";

        case CKR_ATTRIBUTE_SENSITIVE:
            return "CKR_ATTRIBUTE_SENSITIVE";

        case CKR_ATTRIBUTE_TYPE_INVALID:
            return "CKR_ATTRIBUTE_TYPE_INVALID";

        case CKR_ATTRIBUTE_VALUE_INVALID:
            return "CKR_ATTRIBUTE_VALUE_INVALID";

        case CKR_ACTION_PROHIBITED:
            return "CKR_ACTION_PROHIBITED";

        case CKR_DATA_INVALID:
            return "CKR_DATA_INVALID";

        case CKR_DATA_LEN_RANGE:
            return "CKR_DATA_LEN_RANGE";

        case CKR_DEVICE_ERROR:
            return "CKR_DEVICE_ERROR";

        case CKR_DEVICE_MEMORY:
            return "CKR_DEVICE_MEMORY";

        case CKR_DEVICE_REMOVED:
            return "CKR_DEVICE_REMOVED";

        case CKR_ENCRYPTED_DATA_INVALID:
            return "CKR_ENCRYPTED_DATA_INVALID";

        case CKR_ENCRYPTED_DATA_LEN_RANGE:
            return "CKR_ENCRYPTED_DATA_LEN_RANGE";

        case CKR_FUNCTION_CANCELED:
            return "CKR_FUNCTION_CANCELED";

        case CKR_FUNCTION_NOT_PARALLEL:
            return "CKR_FUNCTION_NOT_PARALLEL";

        case CKR_FUNCTION_NOT_SUPPORTED:
            return "CKR_FUNCTION_NOT_SUPPORTED";

        case CKR_KEY_HANDLE_INVALID:
            return "CKR_KEY_HANDLE_INVALID";

        case CKR_KEY_SIZE_RANGE:
            return "CKR_KEY_SIZE_RANGE";

        case CKR_KEY_TYPE_INCONSISTENT:
            return "CKR_KEY_TYPE_INCONSISTENT";

        case CKR_KEY_NOT_NEEDED:
            return "CKR_KEY_NOT_NEEDED";

        case CKR_KEY_CHANGED:
            return "CKR_KEY_CHANGED";

        case CKR_KEY_NEEDED:
            return "CKR_KEY_NEEDED";

        case CKR_KEY_INDIGESTIBLE:
            return "CKR_KEY_INDIGESTIBLE";

        case CKR_KEY_FUNCTION_NOT_PERMITTED:
            return "CKR_KEY_FUNCTION_NOT_PERMITTED";

        case CKR_KEY_NOT_WRAPPABLE:
            return "CKR_KEY_NOT_WRAPPABLE";

        case CKR_KEY_UNEXTRACTABLE:
            return "CKR_KEY_UNEXTRACTABLE";

        case CKR_MECHANISM_INVALID:
            return "CKR_MECHANISM_INVALID";

        case CKR_MECHANISM_PARAM_INVALID:
            return "CKR_MECHANISM_PARAM_INVALID";

        case CKR_OBJECT_HANDLE_INVALID:
            return "CKR_OBJECT_HANDLE_INVALID";

        case CKR_OPERATION_ACTIVE:
            return "CKR_OPERATION_ACTIVE";

        case CKR_OPERATION_NOT_INITIALIZED:
            return "CKR_OPERATION_NOT_INITIALIZED";

        case CKR_PIN_INCORRECT:
            return "CKR_PIN_INCORRECT";

        case CKR_PIN_INVALID:
            return "CKR_PIN_INVALID";

        case CKR_PIN_LEN_RANGE:
            return "CKR_PIN_LEN_RANGE";

        case CKR_PIN_EXPIRED:
            return "CKR_PIN_EXPIRED";

        case CKR_PIN_LOCKED:
            return "CKR_PIN_LOCKED";

        case CKR_SESSION_CLOSED:
            return "CKR_SESSION_CLOSED";

        case CKR_SESSION_COUNT:
            return "CKR_SESSION_COUNT";

        case CKR_SESSION_HANDLE_INVALID:
            return "CKR_SESSION_HANDLE_INVALID";

        case CKR_SESSION_PARALLEL_NOT_SUPPORTED:
            return "CKR_SESSION_PARALLEL_NOT_SUPPORTED";

        case CKR_SESSION_READ_ONLY:
            return "CKR_SESSION_READ_ONLY";

        case CKR_SESSION_EXISTS:
            return "CKR_SESSION_EXISTS";

        case CKR_SESSION_READ_ONLY_EXISTS:
            return "CKR_SESSION_READ_ONLY_EXISTS";

        case CKR_SESSION_READ_WRITE_SO_EXISTS:
            return "CKR_SESSION_READ_WRITE_SO_EXISTS";

        case CKR_SIGNATURE_INVALID:
            return "CKR_SIGNATURE_INVALID";

        case CKR_SIGNATURE_LEN_RANGE:
            return "CKR_SIGNATURE_LEN_RANGE";

        case CKR_TEMPLATE_INCOMPLETE:
            return "CKR_TEMPLATE_INCOMPLETE";

        case CKR_TEMPLATE_INCONSISTENT:
            return "CKR_TEMPLATE_INCONSISTENT";

        case CKR_TOKEN_NOT_PRESENT:
            return "CKR_TOKEN_NOT_PRESENT";

        case CKR_TOKEN_NOT_RECOGNIZED:
            return "CKR_TOKEN_NOT_RECOGNIZED";

        case CKR_TOKEN_WRITE_PROTECTED:
            return "CKR_TOKEN_WRITE_PROTECTED";

        case CKR_UNWRAPPING_KEY_HANDLE_INVALID:
            return "CKR_UNWRAPPING_KEY_HANDLE_INVALID";

        case CKR_UNWRAPPING_KEY_SIZE_RANGE:
            return "CKR_UNWRAPPING_KEY_SIZE_RANGE";

        case CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT:
            return "CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT";

        case CKR_USER_ALREADY_LOGGED_IN:
            return "CKR_USER_ALREADY_LOGGED_IN";

        case CKR_USER_NOT_LOGGED_IN:
            return "CKR_USER_NOT_LOGGED_IN";

        case CKR_USER_PIN_NOT_INITIALIZED:
            return "CKR_USER_PIN_NOT_INITIALIZED";

        case CKR_USER_TYPE_INVALID:
            return "CKR_USER_TYPE_INVALID";

        case CKR_USER_ANOTHER_ALREADY_LOGGED_IN:
            return "CKR_USER_ANOTHER_ALREADY_LOGGED_IN";

        case CKR_USER_TOO_MANY_TYPES:
            return "CKR_USER_TOO_MANY_TYPES";

        case CKR_WRAPPED_KEY_INVALID:
            return "CKR_WRAPPED_KEY_INVALID";

        case CKR_WRAPPED_KEY_LEN_RANGE:
            return "CKR_WRAPPED_KEY_LEN_RANGE";

        case CKR_WRAPPING_KEY_HANDLE_INVALID:
            return "CKR_WRAPPING_KEY_HANDLE_INVALID";

        case CKR_WRAPPING_KEY_SIZE_RANGE:
            return "CKR_WRAPPING_KEY_SIZE_RANGE";

        case CKR_WRAPPING_KEY_TYPE_INCONSISTENT:
            return "CKR_WRAPPING_KEY_TYPE_INCONSISTENT";

        case CKR_RANDOM_SEED_NOT_SUPPORTED:
            return "CKR_RANDOM_SEED_NOT_SUPPORTED";

        case CKR_RANDOM_NO_RNG:
            return "CKR_RANDOM_NO_RNG";

        case CKR_DOMAIN_PARAMS_INVALID:
            return "CKR_DOMAIN_PARAMS_INVALID";

        case CKR_CURVE_NOT_SUPPORTED:
            return "CKR_CURVE_NOT_SUPPORTED";

        case CKR_BUFFER_TOO_SMALL:
            return "CKR_BUFFER_TOO_SMALL";

        case CKR_SAVED_STATE_INVALID:
            return "CKR_SAVED_STATE_INVALID";

        case CKR_INFORMATION_SENSITIVE:
            return "CKR_INFORMATION_SENSITIVE";

        case CKR_STATE_UNSAVEABLE:
            return "CKR_STATE_UNSAVEABLE";

        case CKR_CRYPTOKI_NOT_INITIALIZED:
            return "CKR_CRYPTOKI_NOT_INITIALIZED";

        case CKR_CRYPTOKI_ALREADY_INITIALIZED:
            return "CKR_CRYPTOKI_ALREADY_INITIALIZED";

        case CKR_MUTEX_BAD:
            return "CKR_MUTEX_BAD";

        case CKR_MUTEX_NOT_LOCKED:
            return "CKR_MUTEX_NOT_LOCKED";

        case CKR_NEW_PIN_MODE:
            return "CKR_NEW_PIN_MODE";

        case CKR_NEXT_OTP:
            return "CKR_NEXT_OTP";

        case CKR_EXCEEDED_MAX_ITERATIONS:
            return "CKR_EXCEEDED_MAX_ITERATIONS";

        case CKR_FIPS_SELF_TEST_FAILED:
            return "CKR_FIPS_SELF_TEST_FAILED";

        case CKR_LIBRARY_LOAD_FAILED:
            return "CKR_LIBRARY_LOAD_FAILED";

        case CKR_PIN_TOO_WEAK:
            return "CKR_PIN_TOO_WEAK";

        case CKR_PUBLIC_KEY_INVALID:
            return "CKR_PUBLIC_KEY_INVALID";

        case CKR_FUNCTION_REJECTED:
            return "CKR_FUNCTION_REJECTED";

        case CKR_VENDOR_DEFINED:
            return "CKR_VENDOR_DEFINED";

        default:
            return "UNKNOWN";
    }
}

CK_RV xPKCS11_initMbedtlsPkContext( mbedtls_pk_context * pxMbedtlsPkCtx,
                                    CK_SESSION_HANDLE xSessionHandle,
                                    CK_OBJECT_HANDLE xPkHandle )
{
    CK_RV xResult = CKR_OK;

    CK_KEY_TYPE xKeyType = CKK_VENDOR_DEFINED;
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;

    if( pxMbedtlsPkCtx == NULL )
    {
        xResult = CKR_ARGUMENTS_BAD;
    }
    else if( xSessionHandle == CK_INVALID_HANDLE )
    {
        xResult = CKR_SESSION_HANDLE_INVALID;
    }
    else if( xPkHandle == CK_INVALID_HANDLE )
    {
        xResult = CKR_KEY_HANDLE_INVALID;
    }
    else if( ( C_GetFunctionList( &pxFunctionList ) != CKR_OK ) ||
             ( pxFunctionList == NULL ) ||
             ( pxFunctionList->C_GetAttributeValue == NULL ) )
    {
        xResult = CKR_FUNCTION_FAILED;
    }
    /* Determine key type */
    else
    {
        CK_ATTRIBUTE xAttrTemplate =
        {
            .pValue     = &xKeyType,
            .type       = CKA_KEY_TYPE,
            .ulValueLen = sizeof( CK_KEY_TYPE )
        };

        xResult = pxFunctionList->C_GetAttributeValue( xSessionHandle,
                                                       xPkHandle,
                                                       &xAttrTemplate,
                                                       sizeof( xAttrTemplate ) / sizeof( CK_ATTRIBUTE ) );
    }

    if( xResult == CKR_OK )
    {
        xResult = CKR_FUNCTION_FAILED;

        switch( xKeyType )
        {
            case CKK_ECDSA:
                pxMbedtlsPkCtx->pk_ctx = p11_ecdsa_ctx_alloc();

                if( pxMbedtlsPkCtx->pk_ctx != NULL )
                {
                    xResult = p11_ecdsa_ctx_init( pxMbedtlsPkCtx->pk_ctx,
                                                  pxFunctionList, xSessionHandle, xPkHandle );
                }

                if( xResult == CKR_OK )
                {
                    pxMbedtlsPkCtx->pk_info = &mbedtls_pkcs11_pk_ecdsa;
                }
                else
                {
                    p11_ecdsa_ctx_free( pxMbedtlsPkCtx->pk_ctx );
                    pxMbedtlsPkCtx->pk_ctx = NULL;
                    pxMbedtlsPkCtx->pk_info = NULL;
                }

                break;

            case CKK_RSA:
                pxMbedtlsPkCtx->pk_ctx = p11_rsa_ctx_alloc();

                if( pxMbedtlsPkCtx->pk_ctx != NULL )
                {
                    xResult = p11_rsa_ctx_init( pxMbedtlsPkCtx->pk_ctx,
                                                pxFunctionList, xSessionHandle, xPkHandle );
                }

                if( xResult == CKR_OK )
                {
                    pxMbedtlsPkCtx->pk_info = &mbedtls_pkcs11_pk_rsa;
                }
                else
                {
                    p11_rsa_ctx_free( pxMbedtlsPkCtx->pk_ctx );
                    pxMbedtlsPkCtx->pk_ctx = NULL;
                    pxMbedtlsPkCtx->pk_info = NULL;
                }

                break;

            default:
                pxMbedtlsPkCtx->pk_ctx = NULL;
                pxMbedtlsPkCtx->pk_info = NULL;
                break;
        }
    }

    return xResult;
}

int lPKCS11PkMbedtlsCloseSessionAndFree( mbedtls_pk_context * pxMbedtlsPkCtx )
{
    CK_RV xResult = CKR_OK;
    P11PkCtx_t * pxP11Ctx = NULL;
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;

    configASSERT( pxMbedtlsPkCtx );

    if( pxMbedtlsPkCtx )
    {
        pxP11Ctx = &( ( ( P11EcDsaCtx_t * ) ( pxMbedtlsPkCtx->pk_ctx ) )->xP11PkCtx );
    }
    else
    {
        xResult = CKR_FUNCTION_FAILED;
    }

    if( xResult == CKR_OK )
    {
        xResult = C_GetFunctionList( &pxFunctionList );
    }

    if( xResult == CKR_OK )
    {
        configASSERT( pxFunctionList );
        xResult = pxFunctionList->C_CloseSession( pxP11Ctx->xSessionHandle );
    }

    if( xResult == CKR_OK )
    {
        pxP11Ctx->xSessionHandle = CK_INVALID_HANDLE;
    }

    return( xResult == CKR_OK ? 0 : -1 );
}

int lPKCS11RandomCallback( void * pvCtx,
                           unsigned char * pucOutput,
                           size_t uxLen )
{
    int lRslt;
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;
    CK_SESSION_HANDLE * pxSessionHandle = ( CK_SESSION_HANDLE * ) pvCtx;

    if( pucOutput == NULL )
    {
        lRslt = -1;
    }
    else if( pvCtx == NULL )
    {
        lRslt = -1;
        LogError( "pvCtx must not be NULL." );
    }
    else
    {
        lRslt = ( int ) C_GetFunctionList( &pxFunctionList );
    }

    if( ( lRslt != CKR_OK ) ||
        ( pxFunctionList == NULL ) ||
        ( pxFunctionList->C_GenerateRandom == NULL ) )
    {
        lRslt = -1;
    }
    else
    {
        lRslt = ( int ) pxFunctionList->C_GenerateRandom( *pxSessionHandle, pucOutput, uxLen );
    }

    return lRslt;
}

static void * p11_ecdsa_ctx_alloc( void )
{
    void * pvCtx = NULL;

    pvCtx = mbedtls_calloc( 1, sizeof( P11EcDsaCtx_t ) );

    if( pvCtx != NULL )
    {
        P11EcDsaCtx_t * pxP11EcDsa = ( P11EcDsaCtx_t * ) pvCtx;

        /* Initialize other fields */
        pxP11EcDsa->xP11PkCtx.pxFunctionList = NULL;
        pxP11EcDsa->xP11PkCtx.xSessionHandle = CK_INVALID_HANDLE;
        pxP11EcDsa->xP11PkCtx.xPkHandle = CK_INVALID_HANDLE;

        mbedtls_ecdsa_init( &( pxP11EcDsa->xMbedEcDsaCtx ) );
    }

    return pvCtx;
}

static void p11_ecdsa_ctx_free( void * pvCtx )
{
    if( pvCtx != NULL )
    {
        P11EcDsaCtx_t * pxP11EcDsa = ( P11EcDsaCtx_t * ) pvCtx;

        mbedtls_ecdsa_free( &( pxP11EcDsa->xMbedEcDsaCtx ) );

        mbedtls_free( pvCtx );
    }
}


static CK_RV p11_ecdsa_ctx_init( void * pvCtx,
                                 CK_FUNCTION_LIST_PTR pxFunctionList,
                                 CK_SESSION_HANDLE xSessionHandle,
                                 CK_OBJECT_HANDLE xPkHandle )
{
    CK_RV xResult = CKR_OK;
    P11EcDsaCtx_t * pxP11EcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;
    mbedtls_ecdsa_context * pxMbedEcDsaCtx = NULL;

    configASSERT( pxFunctionList != NULL );
    configASSERT( xSessionHandle != CK_INVALID_HANDLE );
    configASSERT( xPkHandle != CK_INVALID_HANDLE );

    if( pxP11EcDsaCtx != NULL )
    {
        pxMbedEcDsaCtx = &( pxP11EcDsaCtx->xMbedEcDsaCtx );
    }
    else
    {
        xResult = CKR_FUNCTION_FAILED;
    }

    /* Initialize public EC parameter data from attributes */

    CK_ATTRIBUTE pxAttrs[ 2 ] =
    {
        { .type = CKA_EC_PARAMS, .ulValueLen = 0, .pValue = NULL },
        { .type = CKA_EC_POINT,  .ulValueLen = 0, .pValue = NULL }
    };

    /* Determine necessary size */
    xResult = pxFunctionList->C_GetAttributeValue( xSessionHandle,
                                                   xPkHandle,
                                                   pxAttrs,
                                                   sizeof( pxAttrs ) / sizeof( CK_ATTRIBUTE ) );

    if( xResult == CKR_OK )
    {
        if( pxAttrs[ 0 ].ulValueLen > 0 )
        {
            pxAttrs[ 0 ].pValue = pvPortMalloc( pxAttrs[ 0 ].ulValueLen );
        }

        if( pxAttrs[ 1 ].ulValueLen > 0 )
        {
            pxAttrs[ 1 ].pValue = pvPortMalloc( pxAttrs[ 1 ].ulValueLen );
        }

        xResult = pxFunctionList->C_GetAttributeValue( xSessionHandle,
                                                       xPkHandle,
                                                       pxAttrs,
                                                       2 );
    }

    /* Parse EC Group */
    if( xResult == CKR_OK )
    {
        /*TODO: Parse the ECParameters object */
        int lResult = mbedtls_ecp_group_load( &( pxMbedEcDsaCtx->grp ), MBEDTLS_ECP_DP_SECP256R1 );

        if( lResult != 0 )
        {
            xResult = CKR_FUNCTION_FAILED;
        }
    }

    /* Parse ECPoint */
    if( xResult == CKR_OK )
    {
        unsigned char * pucIterator = pxAttrs[ 1 ].pValue;
        size_t uxLen = pxAttrs[ 1 ].ulValueLen;
        int lResult = 0;

        lResult = mbedtls_asn1_get_tag( &pucIterator, &( pucIterator[ uxLen ] ), &uxLen, MBEDTLS_ASN1_OCTET_STRING );

        if( lResult != 0 )
        {
            xResult = CKR_GENERAL_ERROR;
        }
        else
        {
            lResult = mbedtls_ecp_point_read_binary( &( pxMbedEcDsaCtx->grp ),
                                                     &( pxMbedEcDsaCtx->Q ),
                                                     pucIterator,
                                                     uxLen );
        }

        if( lResult != 0 )
        {
            xResult = CKR_GENERAL_ERROR;
        }
    }

    if( pxAttrs[ 0 ].pValue != NULL )
    {
        vPortFree( pxAttrs[ 0 ].pValue );
    }

    if( pxAttrs[ 1 ].pValue != NULL )
    {
        vPortFree( pxAttrs[ 1 ].pValue );
    }

    if( xResult == CKR_OK )
    {
        pxP11EcDsaCtx->xP11PkCtx.pxFunctionList = pxFunctionList;
        pxP11EcDsaCtx->xP11PkCtx.xSessionHandle = xSessionHandle;
        pxP11EcDsaCtx->xP11PkCtx.xPkHandle = xPkHandle;
    }

    return xResult;
}

static int prvASN1WriteBigIntFromOctetStr( unsigned char ** ppucPosition,
                                           const unsigned char * pucStart,
                                           const unsigned char * pucOctetStr,
                                           size_t uxOctetStrLen )
{
    size_t uxRequiredLen = 0;
    int lReturn = 0;

    /* Check if zero byte is needed at beginning */
    if( pucOctetStr[ 0 ] > 0x7F )
    {
        uxRequiredLen = uxOctetStrLen + 1;
    }
    else
    {
        uxRequiredLen = uxOctetStrLen;
    }

    if( &( ( *ppucPosition )[ -uxRequiredLen ] ) >= pucStart )
    {
        *ppucPosition = &( ( *ppucPosition )[ -uxOctetStrLen ] );

        /* Copy octet string */
        ( void ) memcpy( *ppucPosition, pucOctetStr, uxOctetStrLen );

        /* Prepend additional byte if necessary */
        if( pucOctetStr[ 0 ] > 0x7F )
        {
            *ppucPosition = &( ( *ppucPosition )[ -1 ] );
            **ppucPosition = 0;
        }

        lReturn = mbedtls_asn1_write_len( ppucPosition, pucStart, uxRequiredLen );

        if( lReturn > 0 )
        {
            uxRequiredLen += ( size_t ) lReturn;
            lReturn = mbedtls_asn1_write_tag( ppucPosition, pucStart, MBEDTLS_ASN1_INTEGER );
        }

        if( lReturn > 0 )
        {
            lReturn = ( size_t ) lReturn + uxRequiredLen;
        }
    }

    return lReturn;
}

/*
 * SEQUENCE LENGTH (of entire rest of signature)
 *      INTEGER LENGTH  (of R component)
 *      INTEGER LENGTH  (of S component)
 */
static int prvEcdsaSigToASN1InPlace( unsigned char * pucSig,
                                     size_t xSigBufferSize,
                                     size_t * pxSigLen )
{
    unsigned char pucTempBuf[ MBEDTLS_ECDSA_MAX_LEN ] = { 0 };
    unsigned char * pucPosition = pucTempBuf + sizeof( pucTempBuf );

    size_t uxLen = 0;
    int lReturn = 0;
    size_t xComponentLen = *pxSigLen / 2;

    configASSERT( pucSig != NULL );
    configASSERT( pxSigLen != NULL );
    configASSERT( xSigBufferSize > *pxSigLen );

    /* Write "S" portion VLT */
    lReturn = prvASN1WriteBigIntFromOctetStr( &pucPosition, pucTempBuf,
                                              &( pucSig[ xComponentLen ] ), xComponentLen );

    /* Write "R" Portion VLT */
    if( lReturn > 0 )
    {
        uxLen += ( size_t ) lReturn;
        lReturn = prvASN1WriteBigIntFromOctetStr( &pucPosition, pucTempBuf,
                                                  pucSig, xComponentLen );
    }

    if( lReturn > 0 )
    {
        uxLen += ( size_t ) lReturn;
        lReturn = mbedtls_asn1_write_len( &pucPosition, pucTempBuf, uxLen );
    }

    if( lReturn > 0 )
    {
        uxLen += ( size_t ) lReturn;
        lReturn = mbedtls_asn1_write_tag( &pucPosition, pucTempBuf,
                                          MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE );
    }

    if( lReturn > 0 )
    {
        uxLen += ( size_t ) lReturn;
    }

    if( ( lReturn > 0 ) && ( uxLen <= xSigBufferSize ) )
    {
        ( void ) memcpy( pucSig, pucPosition, uxLen );
        *pxSigLen = uxLen;
        lReturn = 0;
    }
    else
    {
        lReturn = -1;
    }

    return lReturn;
}

static int p11_ecdsa_sign( void * pvCtx,
                           mbedtls_md_type_t xMdAlg,
                           const unsigned char * pucHash,
                           size_t xHashLen,
                           unsigned char * pucSig,
                           size_t xSigBufferSize,
                           size_t * pxSigLen,
                           int ( * plRng )( void *, unsigned char *, size_t ),
                           void * pvRng )
{
    CK_RV xResult = CKR_OK;
    int32_t lFinalResult = 0;
    const P11EcDsaCtx_t * pxEcDsaCtx = NULL;
    const P11PkCtx_t * pxP11Ctx = NULL;
    unsigned char pucHashCopy[ xHashLen ];

    CK_MECHANISM xMech =
    {
        .mechanism      = CKM_ECDSA,
        .pParameter     = NULL,
        .ulParameterLen = 0
    };

    /* Unused parameters. */
    ( void ) ( xMdAlg );
    ( void ) ( plRng );
    ( void ) ( pvRng );

    configASSERT( pucSig != NULL );
    configASSERT( xSigBufferSize > 0 );
    configASSERT( pxSigLen != NULL );
    configASSERT( pucHash != NULL );
    configASSERT( xHashLen > 0 );

    if( pvCtx != NULL )
    {
        pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;
        pxP11Ctx = &( pxEcDsaCtx->xP11PkCtx );
    }
    else
    {
        xResult = CKR_FUNCTION_FAILED;
    }

    if( CKR_OK == xResult )
    {
        /* Use the PKCS#11 module to sign. */
        xResult = pxP11Ctx->pxFunctionList->C_SignInit( pxP11Ctx->xSessionHandle,
                                                        &xMech,
                                                        pxP11Ctx->xPkHandle );
    }

    if( CKR_OK == xResult )
    {
        CK_ULONG ulSigLen = xSigBufferSize;

        ( void ) memcpy( pucHashCopy, pucHash, xHashLen );

        xResult = pxP11Ctx->pxFunctionList->C_Sign( pxP11Ctx->xSessionHandle,
                                                    pucHashCopy, xHashLen,
                                                    pucSig, &ulSigLen );

        if( xResult == CKR_OK )
        {
            *pxSigLen = ulSigLen;
        }
    }

    if( xResult != CKR_OK )
    {
        LogError( "Failed to sign message using PKCS #11 with error code %02X.", xResult );
        lFinalResult = -1;
    }
    else
    {
        lFinalResult = prvEcdsaSigToASN1InPlace( pucSig, xSigBufferSize, pxSigLen );
    }

    return lFinalResult;
}

/* Shim functions */
static size_t p11_ecdsa_get_bitlen( const void * pvCtx )
{
    P11EcDsaCtx_t * pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;

    configASSERT( mbedtls_ecdsa_info.get_bitlen );

    return mbedtls_ecdsa_info.get_bitlen( &( pxEcDsaCtx->xMbedEcDsaCtx ) );
}

static int p11_ecdsa_can_do( mbedtls_pk_type_t xType )
{
    return( xType == MBEDTLS_PK_ECDSA );
}

static int p11_ecdsa_verify( void * pvCtx,
                             mbedtls_md_type_t xMdAlg,
                             const unsigned char * pucHash,
                             size_t xHashLen,
                             const unsigned char * pucSig,
                             size_t xSigLen )
{
    P11EcDsaCtx_t * pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;

    configASSERT( mbedtls_ecdsa_info.verify_func );

    return mbedtls_ecdsa_info.verify_func( &( pxEcDsaCtx->xMbedEcDsaCtx ),
                                           xMdAlg,
                                           pucHash, xHashLen,
                                           pucSig, xSigLen );
}

static int p11_ecdsa_check_pair( const void * pvPub,
                                 const void * pvPrv,
                                 int ( * lFRng )( void *, unsigned char *, size_t ),
                                 void * pvPRng )
{
    mbedtls_ecp_keypair * pxPubKey = ( mbedtls_ecp_keypair * ) pvPub;
    mbedtls_ecp_keypair * pxPrvKey = ( mbedtls_ecp_keypair * ) pvPrv;

    P11EcDsaCtx_t * pxP11PrvKey = ( P11EcDsaCtx_t * ) pvPrv;
    int lResult = 0;

    ( void ) lFRng;
    ( void ) pvPRng;

    if( ( pxPubKey == NULL ) || ( pxPrvKey == NULL ) )
    {
        lResult = -1;
    }

    if( lResult == 0 )
    {
        if( ( pxPubKey->grp.id == MBEDTLS_ECP_DP_NONE ) ||
            ( pxPubKey->grp.id != pxPrvKey->grp.id ) )
        {
            lResult = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
        }
    }

    /* Compare public key points */
    if( lResult == 0 )
    {
        lResult = mbedtls_mpi_cmp_mpi( &( pxPubKey->Q.X ), &( pxPrvKey->Q.X ) );
    }

    if( lResult == 0 )
    {
        lResult = mbedtls_mpi_cmp_mpi( &( pxPubKey->Q.Y ), &( pxPrvKey->Q.Y ) );
    }

    if( lResult == 0 )
    {
        lResult = mbedtls_mpi_cmp_mpi( &( pxPubKey->Q.Z ), &( pxPrvKey->Q.Z ) );
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
        lResult = p11_ecdsa_sign( ( void * ) ( void * ) pvPrv, MBEDTLS_MD_SHA256,
                                  pucTestHash, sizeof( pucTestHash ),
                                  pucTestSignature, sizeof( pucTestSignature ), &uxSigLen,
                                  NULL, NULL );

        if( lResult == 0 )
        {
            lResult = mbedtls_ecdsa_read_signature( pxPubKey,
                                                    pucTestHash, sizeof( pucTestHash ),
                                                    pucTestSignature, uxSigLen );
        }
    }

    return lResult;
}

static void p11_ecdsa_debug( const void * pvCtx,
                             mbedtls_pk_debug_item * pxItems )
{
    P11EcDsaCtx_t * pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;

    configASSERT( mbedtls_ecdsa_info.debug_func );

    return mbedtls_ecdsa_info.debug_func( &( pxEcDsaCtx->xMbedEcDsaCtx ), pxItems );
}



static size_t p11_rsa_get_bitlen( const void * pvCtx )
{
    P11RsaCtx_t * pxRsaCtx = ( P11RsaCtx_t * ) pvCtx;

    configASSERT( mbedtls_rsa_info.get_bitlen );

    return mbedtls_rsa_info.get_bitlen( &( pxRsaCtx->xMbedRsaCtx ) );
}

static int p11_rsa_can_do( mbedtls_pk_type_t xType )
{
    return( xType == MBEDTLS_PK_RSA );
}

static int p11_rsa_verify( void * pvCtx,
                           mbedtls_md_type_t xMdAlg,
                           const unsigned char * pucHash,
                           size_t xHashLen,
                           const unsigned char * pucSig,
                           size_t xSigLen )
{
    P11RsaCtx_t * pxRsaCtx = ( P11RsaCtx_t * ) pvCtx;

    configASSERT( mbedtls_rsa_info.verify_func );

    return mbedtls_rsa_info.verify_func( &( pxRsaCtx->xMbedRsaCtx ),
                                         xMdAlg,
                                         pucHash, xHashLen,
                                         pucSig, xSigLen );
}

static int p11_rsa_sign( void * ctx,
                         mbedtls_md_type_t md_alg,
                         const unsigned char * hash,
                         size_t hash_len,
                         unsigned char * sig,
                         size_t sig_size,
                         size_t * sig_len,
                         int ( * f_rng )( void *, unsigned char *, size_t ),
                         void * p_rng )
{
    /*TODO: Not implemented yet. */
    ( void ) ctx;
    ( void ) md_alg;
    ( void ) hash;
    ( void ) hash_len;
    ( void ) sig;
    ( void ) sig_size;
    ( void ) sig_len;
    ( void ) f_rng;
    ( void ) p_rng;

    return -1;
}


static int p11_rsa_check_pair( const void * pvPub,
                               const void * pvPrv,
                               int ( * lFRng )( void *, unsigned char *, size_t ),
                               void * pvPRng )
{
    P11RsaCtx_t * pxP11RsaCtx = ( P11RsaCtx_t * ) pvPrv;

    configASSERT( mbedtls_rsa_info.check_pair_func );

    return mbedtls_rsa_info.check_pair_func( pvPub, &( pxP11RsaCtx->xMbedRsaCtx ),
                                             lFRng, pvPRng );
}

static void * p11_rsa_ctx_alloc( void )
{
    void * pvCtx = NULL;

    pvCtx = mbedtls_calloc( 1, sizeof( P11RsaCtx_t ) );

    if( pvCtx != NULL )
    {
        P11RsaCtx_t * pxP11Rsa = ( P11RsaCtx_t * ) pvCtx;

        /* Initialize other fields */
        pxP11Rsa->xP11PkCtx.pxFunctionList = NULL;
        pxP11Rsa->xP11PkCtx.xSessionHandle = CK_INVALID_HANDLE;
        pxP11Rsa->xP11PkCtx.xPkHandle = CK_INVALID_HANDLE;

        mbedtls_rsa_init( &( pxP11Rsa->xMbedRsaCtx ) );
    }

    return pvCtx;
}

static CK_RV p11_rsa_ctx_init( void * pvCtx,
                               CK_FUNCTION_LIST_PTR pxFunctionList,
                               CK_SESSION_HANDLE xSessionHandle,
                               CK_OBJECT_HANDLE xPkHandle )
{
    CK_RV xResult = CKR_OK;
    P11RsaCtx_t * pxP11RsaCtx = ( P11RsaCtx_t * ) pvCtx;
    mbedtls_rsa_context * pxMbedRsaCtx = NULL;

    configASSERT( pxFunctionList != NULL );
    configASSERT( xSessionHandle != CK_INVALID_HANDLE );
    configASSERT( xPkHandle != CK_INVALID_HANDLE );

    if( pxP11RsaCtx != NULL )
    {
        pxMbedRsaCtx = &( pxP11RsaCtx->xMbedRsaCtx );
    }
    else
    {
        xResult = CKR_FUNCTION_FAILED;
    }

    ( void ) pxMbedRsaCtx;

    return xResult;
}

static void p11_rsa_ctx_free( void * pvCtx )
{
    if( pvCtx != NULL )
    {
        P11RsaCtx_t * pxP11Rsa = ( P11RsaCtx_t * ) pvCtx;

        mbedtls_rsa_free( &( pxP11Rsa->xMbedRsaCtx ) );

        mbedtls_free( pvCtx );
    }
}

static void p11_rsa_debug( const void * pvCtx,
                           mbedtls_pk_debug_item * pxItems )
{
    P11RsaCtx_t * pxP11RsaCtx = ( P11RsaCtx_t * ) pvCtx;

    configASSERT( mbedtls_rsa_info.debug_func );

    return mbedtls_rsa_info.debug_func( &( pxP11RsaCtx->xMbedRsaCtx ), pxItems );
}

#endif /* MBEDTLS_TRANSPORT_PKCS11 */
