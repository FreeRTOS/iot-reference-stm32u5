/*
 * FreeRTOS STM32 Reference Integration
 *
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include "logging_levels.h"
#define LOG_LEVEL    LOG_DEBUG
#include "logging.h"

#include "FreeRTOS.h"

#include <string.h>
#include <stdlib.h>

#include "tls_transport_config.h"

#ifdef MBEDTLS_TRANSPORT_PKCS11

#include "mbedtls_transport.h"
#include "mbedtls_error_utils.h"

#include "core_pkcs11.h"
#include "core_pkcs11_config.h"
#include "pkcs11.h"

#include "PkiObject.h"
#include "PkiObject_prv.h"

#include "mbedtls/pem.h"
#include "mbedtls/base64.h"
#include "mbedtls/x509_csr.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/x509.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform.h"
#include "pk_wrap.h"
#include "mbedtls/ecp.h"


static CK_RV xPrvExportPubKeyDer( CK_SESSION_HANDLE xSession,
                                  CK_OBJECT_HANDLE xPublicKeyHandle,
                                  uint8_t ** ppucPubKeyDer,
                                  uint32_t * pulPubKeyDerLen );

static CK_RV xPrvDestoryObject( CK_SESSION_HANDLE xSessionHandle,
                                CK_OBJECT_CLASS xClass,
                                char * pcLabel,
                                size_t uxLabelLen );

/*-----------------------------------------------------------*/

/*TODO: implement CKA_PUBLIC_KEY_INFO on the backend to make this compliant with the standard and reduce unnecessary memory allocation / deallocation. */
/* Caller must free the returned buffer */
static CK_RV xPrvExportPubKeyDer( CK_SESSION_HANDLE xSession,
                                  CK_OBJECT_HANDLE xPublicKeyHandle,
                                  uint8_t ** ppucPubKeyDer,
                                  uint32_t * pulPubKeyDerLen )
{
    CK_RV xResult;
    /* Query the key size. */
    CK_ATTRIBUTE xTemplate = { 0 };
    CK_FUNCTION_LIST_PTR pxFunctionList;

    *ppucPubKeyDer = NULL;
    *pulPubKeyDerLen = 0;

    const uint8_t pucEcP256AsnAndOid[] =
    {
        0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
        0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
        0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
        0x42, 0x00
    };

    const uint8_t pucUnusedKeyTag[] = { 0x04, 0x41 };

    ( void ) pucUnusedKeyTag;

    xResult = C_GetFunctionList( &pxFunctionList );

    if( CKR_OK == xResult )
    {
        /* Query public key size */
        xTemplate.type = CKA_EC_POINT;
        xTemplate.ulValueLen = 0;
        xTemplate.pValue = NULL;
        xResult = pxFunctionList->C_GetAttributeValue( xSession,
                                                       xPublicKeyHandle,
                                                       &xTemplate,
                                                       1 );
    }

    if( CKR_OK == xResult )
    {
        /* Add space for DER Header */
        *pulPubKeyDerLen = xTemplate.ulValueLen + sizeof( pucEcP256AsnAndOid ) - sizeof( pucUnusedKeyTag ) + 1;

        /* Allocate a buffer for the DER form  of the key */
        *ppucPubKeyDer = pvPortMalloc( *pulPubKeyDerLen );

        xResult = CKR_FUNCTION_FAILED;
    }

    /* Read public key into buffer */
    if( *ppucPubKeyDer != NULL )
    {
        memset( *ppucPubKeyDer, 0, *pulPubKeyDerLen );

        xTemplate.pValue = &( ( *ppucPubKeyDer )[ sizeof( pucEcP256AsnAndOid ) - sizeof( pucUnusedKeyTag ) ] );

        /* xTemplate.ulValueLen remains the same as in the last call */

        xResult = pxFunctionList->C_GetAttributeValue( xSession,
                                                       xPublicKeyHandle,
                                                       &xTemplate,
                                                       1 );

        /* Copy DER header */
        ( void ) memcpy( *ppucPubKeyDer, pucEcP256AsnAndOid, sizeof( pucEcP256AsnAndOid ) );
    }
    else
    {
        LogError( "Failed to allocate %ld bytes for public key in DER format.", *pulPubKeyDerLen );
        xResult = CKR_HOST_MEMORY;
    }

    if( xResult != CKR_OK )
    {
        if( *ppucPubKeyDer != NULL )
        {
            vPortFree( *ppucPubKeyDer );
            *ppucPubKeyDer = NULL;
        }

        *pulPubKeyDerLen = 0;
    }

    return xResult;
}

/*-----------------------------------------------------------*/

static CK_RV xPrvDestoryObject( CK_SESSION_HANDLE xSessionHandle,
                                CK_OBJECT_CLASS xClass,
                                char * pcLabel,
                                size_t uxLabelLen )
{
    CK_RV xResult = CKR_OK;
    CK_OBJECT_HANDLE xObjectHandle = 0;
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;

    configASSERT( xSessionHandle );
    configASSERT( pcLabel );
    configASSERT( uxLabelLen > 0 );

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        xResult = xFindObjectWithLabelAndClass( xSessionHandle,
                                                pcLabel, uxLabelLen,
                                                xClass,
                                                &xObjectHandle );
    }

    if( ( xResult == CKR_OK ) &&
        ( xObjectHandle != CK_INVALID_HANDLE ) )
    {
        xResult = pxFunctionList->C_DestroyObject( xSessionHandle, xObjectHandle );
    }

    return xResult;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPrvCkRvToPkiStatus( CK_RV xError )
{
    PkiStatus_t xStatus;

    switch( xError )
    {
        case CKR_OK:
            xStatus = PKI_SUCCESS;
            break;

        case CKR_OBJECT_HANDLE_INVALID:
            xStatus = PKI_ERR_OBJ_NOT_FOUND;
            break;

        case CKR_HOST_MEMORY:
            xStatus = PKI_ERR_NOMEM;
            break;

        case CKR_FUNCTION_FAILED:
            xStatus = PKI_ERR_INTERNAL;
            break;

        default:
            xStatus = PKI_ERR;
            break;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkcs11InitMbedtlsPkContext( const char * pcLabel,
                                         mbedtls_pk_context * pxPkCtx,
                                         CK_SESSION_HANDLE_PTR pxSessionHandle )
{
    char pcLabelBuffer[ pkcs11configMAX_LABEL_LENGTH + 1 ];
    PkiStatus_t xStatus = PKI_SUCCESS;
    size_t uxLabelLen = 0;

    if( !pcLabel )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pcLabel cannot be NULL." );
    }
    else if( !pxPkCtx )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pxPkCtx cannot be NULL." );
    }
    else
    {
        uxLabelLen = strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH );
        ( void ) strncpy( pcLabelBuffer, pcLabel, pkcs11configMAX_LABEL_LENGTH + 1 );
    }

    if( uxLabelLen == 0 )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pcLabel must have a length > 0." );
    }
    else
    {
        CK_SESSION_HANDLE xSession = CK_INVALID_HANDLE;
        CK_OBJECT_HANDLE xPkHandle = CK_INVALID_HANDLE;
        CK_RV xResult;

        xResult = xInitializePkcs11Session( &xSession );

        if( xResult != CKR_OK )
        {
            LogError( "Failed to initialize PKCS11 session. CK_RV: %s",
                      pcPKCS11StrError( xResult ) );

            xStatus = xPrvCkRvToPkiStatus( xResult );
        }

        if( xStatus == PKI_SUCCESS )
        {
            xResult = xFindObjectWithLabelAndClass( xSession,
                                                    pcLabelBuffer, uxLabelLen,
                                                    CKO_PRIVATE_KEY, &xPkHandle );

            if( ( xResult != CKR_OK ) ||
                ( xPkHandle == CK_INVALID_HANDLE ) )
            {
                LogError( "Failed to find private key with label: %s in PKCS#11 module. CK_RV: %s",
                          pcLabelBuffer, pcPKCS11StrError( xResult ) );
                xStatus = xPrvCkRvToPkiStatus( xResult );
            }
        }

        if( xStatus == PKI_SUCCESS )
        {
            xResult = xPKCS11_initMbedtlsPkContext( pxPkCtx, xSession, xPkHandle );
            xStatus = xPrvCkRvToPkiStatus( xResult );
        }

        if( ( xStatus == PKI_SUCCESS ) &&
            ( pxSessionHandle != NULL ) )
        {
            *pxSessionHandle = xSession;
        }
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkcs11WriteCertificate( const char * pcLabel,
                                     const mbedtls_x509_crt * pxCertificateContext )
{
    char pcLabelBuffer[ pkcs11configMAX_LABEL_LENGTH + 1 ];
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;
    PkiStatus_t xStatus = PKI_SUCCESS;
    CK_SESSION_HANDLE xSession = 0;
    size_t uxLabelLen = 0;
    CK_RV xResult = CKR_OK;


    if( !pcLabel )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pcLabel cannot be NULL." );
    }
    else if( !pxCertificateContext )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pxCertificateContext cannot be NULL." );
    }
    else
    {
        uxLabelLen = strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH );
        ( void ) strncpy( pcLabelBuffer, pcLabel, pkcs11configMAX_LABEL_LENGTH + 1 );
    }

    if( uxLabelLen == 0 )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pcLabel must have a length > 0." );
    }
    else
    {
        xResult = C_GetFunctionList( &pxFunctionList );
    }

    if( xResult != CKR_OK )
    {
        LogError( "Failed to get PKCS11 function list pointer. CK_RV: %s",
                  pcPKCS11StrError( xResult ) );

        xStatus = xPrvCkRvToPkiStatus( xResult );
    }

    if( xStatus == PKI_SUCCESS )
    {
        xResult = xInitializePkcs11Session( &xSession );

        if( xResult != CKR_OK )
        {
            LogError( "Failed to initialize PKCS11 session. CK_RV: %s",
                      pcPKCS11StrError( xResult ) );

            xStatus = xPrvCkRvToPkiStatus( xResult );
        }
    }

    if( xStatus == PKI_SUCCESS )
    {
        xResult = xPrvDestoryObject( xSession, CKO_CERTIFICATE, pcLabelBuffer, uxLabelLen );

        if( xResult != CKR_OK )
        {
            LogError( "Failed to delete existing PKCS11 object. CK_RV: %s",
                      pcPKCS11StrError( xResult ) );

            xStatus = xPrvCkRvToPkiStatus( xResult );
        }
    }

    if( xStatus == PKI_SUCCESS )
    {
        int lRslt = lWriteCertificateToPKCS11( pxCertificateContext,
                                               xSession,
                                               pcLabelBuffer, uxLabelLen );

        xStatus = xPrvMbedtlsErrToPkiStatus( lRslt );
    }

    if( xSession )
    {
        pxFunctionList->C_CloseSession( xSession );
        xSession = 0;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkcs11ReadCertificate( mbedtls_x509_crt * pxCertificateContext,
                                    const char * pcLabel )
{
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;
    PkiStatus_t xStatus = PKI_SUCCESS;
    CK_SESSION_HANDLE xSession;
    size_t uxLabelLen = 0;

    if( !pcLabel )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pcLabel cannot be NULL." );
    }
    else if( !pxCertificateContext )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pxCertificateContext cannot be NULL." );
    }
    else
    {
        uxLabelLen = strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH );

        if( uxLabelLen == 0 )
        {
            xStatus = PKI_ERR_ARG_INVALID;
            LogError( "pcLabel must have a length > 0." );
        }
    }

    if( xStatus == PKI_SUCCESS )
    {
        CK_RV xResult = C_GetFunctionList( &pxFunctionList );

        if( ( xResult != CKR_OK ) || ( pxFunctionList == NULL ) )
        {
            LogError( "Failed to get PKCS11 function list pointer. CK_RV: %s",
                      pcPKCS11StrError( xResult ) );

            xStatus = xPrvCkRvToPkiStatus( xResult );
        }
    }

    if( xStatus == PKI_SUCCESS )
    {
        CK_RV xResult = xInitializePkcs11Session( &xSession );

        if( xResult != CKR_OK )
        {
            LogError( "Failed to initialize PKCS11 session. CK_RV: %s",
                      pcPKCS11StrError( xResult ) );

            xStatus = xPrvCkRvToPkiStatus( xResult );
        }
    }

    if( xStatus == PKI_SUCCESS )
    {
        int lRslt = 0;
        mbedtls_x509_crt_init( pxCertificateContext );

        lRslt = lReadCertificateFromPKCS11( pxCertificateContext,
                                            xSession,
                                            pcLabel, uxLabelLen );

        MBEDTLS_LOG_IF_ERROR( lRslt, "Failed to parse certificate(s) from pkcs11 label: %.*s,",
                              uxLabelLen, pcLabel );

        xStatus = xPrvMbedtlsErrToPkiStatus( lRslt );
    }

    if( pxFunctionList && xSession )
    {
        pxFunctionList->C_CloseSession( xSession );
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkcs11GenerateKeyPairEC( char * pcPrivateKeyLabel,
                                      char * pcPublicKeyLabel,
                                      unsigned char ** ppucPublicKeyDer,
                                      size_t * puxPublicKeyDerLen )
{
    CK_RV xResult;
    CK_SESSION_HANDLE xSession = 0;
    CK_OBJECT_HANDLE xPrvKeyHandle = 0;
    CK_OBJECT_HANDLE xPubKeyHandle = 0;
    size_t uxPrivateKeyLabelLen = 0;
    size_t uxPublicKeyLabelLen = 0;
    char pcPubKeyLabelBuffer[ pkcs11configMAX_LABEL_LENGTH + 1 ];
    char pcPrvKeyLabelBuffer[ pkcs11configMAX_LABEL_LENGTH + 1 ];

    CK_FUNCTION_LIST_PTR pxFunctionList;

    uint32_t ulPubKeyLen = 0;

    configASSERT( pcPrivateKeyLabel );
    configASSERT( pcPublicKeyLabel );
    configASSERT( ppucPublicKeyDer );
    configASSERT( puxPublicKeyDerLen );

    ( void ) strncpy( pcPrvKeyLabelBuffer, pcPrivateKeyLabel, pkcs11configMAX_LABEL_LENGTH );
    ( void ) strncpy( pcPubKeyLabelBuffer, pcPublicKeyLabel, pkcs11configMAX_LABEL_LENGTH );


    uxPrivateKeyLabelLen = strnlen( pcPrivateKeyLabel, pkcs11configMAX_LABEL_LENGTH );
    uxPublicKeyLabelLen = strnlen( pcPublicKeyLabel, pkcs11configMAX_LABEL_LENGTH );

    configASSERT( uxPrivateKeyLabelLen > 0 );
    configASSERT( uxPublicKeyLabelLen > 0 );

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Session( &xSession );
    }

    if( xResult == CKR_OK )
    {
        xResult = xPrvDestoryObject( xSession, CKO_PRIVATE_KEY, pcPrivateKeyLabel, uxPrivateKeyLabelLen );
    }

    if( xResult == CKR_OK )
    {
        xResult = xPrvDestoryObject( xSession, CKO_PUBLIC_KEY, pcPublicKeyLabel, uxPublicKeyLabelLen );
    }

    if( xResult == CKR_OK )
    {
        CK_MECHANISM xMechanism = { CKM_EC_KEY_PAIR_GEN, NULL_PTR, 0 };
        CK_BYTE xEcParams[] = pkcs11DER_ENCODED_OID_P256; /* prime256v1 */
        CK_KEY_TYPE xKeyType = CKK_EC;
        CK_BBOOL xTrue = CK_TRUE;
        CK_ATTRIBUTE xPrivateKeyTemplate[] =
        {
            { CKA_KEY_TYPE, &xKeyType,           sizeof( xKeyType )   },
            { CKA_TOKEN,    &xTrue,              sizeof( xTrue )      },
            { CKA_PRIVATE,  &xTrue,              sizeof( xTrue )      },
            { CKA_SIGN,     &xTrue,              sizeof( xTrue )      },
            { CKA_LABEL,    pcPrvKeyLabelBuffer, uxPrivateKeyLabelLen }
        };
        CK_ATTRIBUTE xPublicKeyTemplate[] =
        {
            { CKA_KEY_TYPE,  &xKeyType,           sizeof( xKeyType )  },
            { CKA_VERIFY,    &xTrue,              sizeof( xTrue )     },
            { CKA_EC_PARAMS, xEcParams,           sizeof( xEcParams ) },
            { CKA_LABEL,     pcPubKeyLabelBuffer, uxPublicKeyLabelLen }
        };
        xResult = pxFunctionList->C_GenerateKeyPair( xSession,
                                                     &xMechanism,
                                                     &( xPublicKeyTemplate[ 0 ] ),
                                                     sizeof( xPublicKeyTemplate ) / sizeof( CK_ATTRIBUTE ),
                                                     &( xPrivateKeyTemplate[ 0 ] ),
                                                     sizeof( xPrivateKeyTemplate ) / sizeof( CK_ATTRIBUTE ),
                                                     &xPubKeyHandle,
                                                     &xPrvKeyHandle );
    }

    if( xResult == CKR_OK )
    {
        configASSERT( xPubKeyHandle );
        xResult = xPrvExportPubKeyDer( xSession,
                                       xPubKeyHandle,
                                       ppucPublicKeyDer, &ulPubKeyLen );
    }

    if( xResult == CKR_OK )
    {
        *puxPublicKeyDerLen = ulPubKeyLen;
    }

    if( pxFunctionList &&
        xSession )
    {
        pxFunctionList->C_CloseSession( xSession );
    }

    return xPrvCkRvToPkiStatus( xResult );
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkcs11ReadPublicKey( unsigned char ** ppucPublicKeyDer,
                                  size_t * puxPubKeyLen,
                                  const char * pcPubKeyLabel )
{
    PkiStatus_t xStatus = PKI_SUCCESS;

    CK_SESSION_HANDLE xSession = 0;
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_OBJECT_HANDLE xPubKeyHandle = 0;

    uint32_t ulPubKeyLen = 0;

    size_t uxPubKeyLabelLen = 0;
    char pcPubKeyLabelBuf[ pkcs11configMAX_LABEL_LENGTH + 1 ];

    if( ( ppucPublicKeyDer == NULL ) || ( pcPubKeyLabel == NULL ) )
    {
        xStatus = PKI_ERR_ARG_INVALID;
    }
    else
    {
        uxPubKeyLabelLen = strnlen( pcPubKeyLabel, pkcs11configMAX_LABEL_LENGTH + 1 );

        if( ( uxPubKeyLabelLen == 0 ) ||
            ( uxPubKeyLabelLen > pkcs11configMAX_LABEL_LENGTH ) )
        {
            xStatus = PKI_ERR_ARG_INVALID;
        }
    }

    if( xStatus == PKI_SUCCESS )
    {
        CK_RV xResult;

        ( void ) strncpy( pcPubKeyLabelBuf, pcPubKeyLabel, pkcs11configMAX_LABEL_LENGTH );

        xResult = C_GetFunctionList( &pxFunctionList );
        xStatus = xPrvCkRvToPkiStatus( xResult );
    }

    if( xStatus == PKI_SUCCESS )
    {
        CK_RV xResult = xInitializePkcs11Session( &xSession );
        xStatus = xPrvCkRvToPkiStatus( xResult );
    }

    if( xStatus == PKI_SUCCESS )
    {
        CK_RV xResult = xFindObjectWithLabelAndClass( xSession,
                                                      pcPubKeyLabelBuf,
                                                      uxPubKeyLabelLen,
                                                      CKO_PUBLIC_KEY,
                                                      &xPubKeyHandle );
        xStatus = xPrvCkRvToPkiStatus( xResult );
    }

    if( xStatus == PKI_SUCCESS )
    {
        CK_RV xResult = xPrvExportPubKeyDer( xSession,
                                             xPubKeyHandle,
                                             ppucPublicKeyDer, &ulPubKeyLen );
        xStatus = xPrvCkRvToPkiStatus( xResult );
    }

    if( xStatus == PKI_SUCCESS )
    {
        *puxPubKeyLen = ( size_t ) ulPubKeyLen;
    }

    if( xSession )
    {
        CK_RV xResult = pxFunctionList->C_CloseSession( xSession );
        xStatus = xPrvCkRvToPkiStatus( xResult );
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkcs11WritePubKey( const char * pcLabel,
                                const mbedtls_pk_context * pxPubKeyContext )
{
    char pcLabelBuffer[ pkcs11configMAX_LABEL_LENGTH + 1 ];
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;
    PkiStatus_t xStatus = PKI_SUCCESS;
    CK_SESSION_HANDLE xSession = 0;
    size_t uxLabelLen = 0;
    CK_RV xResult = CKR_OK;


    if( !pcLabel )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pcLabel cannot be NULL." );
    }
    else if( !pxPubKeyContext )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pxCertificateContext cannot be NULL." );
    }
    else
    {
        uxLabelLen = strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH );
        ( void ) strncpy( pcLabelBuffer, pcLabel, pkcs11configMAX_LABEL_LENGTH + 1 );
    }

    if( uxLabelLen == 0 )
    {
        xStatus = PKI_ERR_ARG_INVALID;
        LogError( "pcLabel must have a length > 0." );
    }
    else
    {
        xResult = C_GetFunctionList( &pxFunctionList );
    }

    if( xResult != CKR_OK )
    {
        LogError( "Failed to get PKCS11 function list pointer. CK_RV: %s",
                  pcPKCS11StrError( xResult ) );

        xStatus = xPrvCkRvToPkiStatus( xResult );
    }

    if( xStatus == PKI_SUCCESS )
    {
        xResult = xInitializePkcs11Session( &xSession );

        if( xResult != CKR_OK )
        {
            LogError( "Failed to initialize PKCS11 session. CK_RV: %s",
                      pcPKCS11StrError( xResult ) );

            xStatus = xPrvCkRvToPkiStatus( xResult );
        }
    }

    if( xStatus == PKI_SUCCESS )
    {
        xResult = xPrvDestoryObject( xSession, CKO_PUBLIC_KEY, pcLabelBuffer, uxLabelLen );

        if( xResult != CKR_OK )
        {
            LogError( "Failed to delete existing PKCS11 object. CK_RV: %s",
                      pcPKCS11StrError( xResult ) );

            xStatus = xPrvCkRvToPkiStatus( xResult );
        }
    }

    if( xStatus == PKI_SUCCESS )
    {
        int lRslt = lWriteEcPublicKeyToPKCS11( pxPubKeyContext,
                                               xSession,
                                               pcLabelBuffer, uxLabelLen );

        xStatus = xPrvMbedtlsErrToPkiStatus( lRslt );
    }

    if( xSession )
    {
        pxFunctionList->C_CloseSession( xSession );
        xSession = 0;
    }

    return xStatus;
}

#endif /* MBEDTLS_TRANSPORT_PKCS11 */
