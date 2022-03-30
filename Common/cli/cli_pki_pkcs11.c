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

#include "core_pkcs11.h"
#include "core_pkcs11_config.h"
#include "pkcs11.h"

#include "cli_pki_prv.h"

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
                                char * pcLabel );

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
        LogDebug( "Allocated %ld bytes for public key in DER format.", *pulPubKeyDerLen );

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

static CK_RV xPrvDestoryObject( CK_SESSION_HANDLE xSessionHandle,
                                CK_OBJECT_CLASS xClass,
                                char * pcLabel )
{
    CK_RV xResult = CKR_OK;
    CK_OBJECT_HANDLE xObjectHandle = 0;
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;

    configASSERT( xSessionHandle );
    configASSERT( pcLabel );

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        xResult = xFindObjectWithLabelAndClass( xSessionHandle,
                                                pcLabel, strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH ),
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


BaseType_t xPkcs11InitMbedtlsPkContext( char * pcLabel,
                                        mbedtls_pk_context * pxPkCtx,
                                        CK_SESSION_HANDLE_PTR pxSessionHandle )
{
    CK_OBJECT_HANDLE xPkHandle;
    size_t uxLabelLen = 0;
    CK_RV xResult;

    int lError = 0;

    if( !pcLabel )
    {
        lError = -1;
        LogError( "pcLabel cannot be NULL." );
    }
    else if( !pxPkCtx )
    {
        lError = -1;
        LogError( "pxPkCtx cannot be NULL." );
    }
    else
    {
        uxLabelLen = strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH );
    }

    if( uxLabelLen == 0 )
    {
        lError = -1;
        LogError( "pcLabel must have a length > 0." );
    }
    else
    {
        xResult = xInitializePkcs11Token();

        if( xResult == CKR_OK )
        {
            xResult = xInitializePkcs11Session( pxSessionHandle );
        }

        if( xResult == CKR_OK )
        {
            xResult = xFindObjectWithLabelAndClass( *pxSessionHandle,
                                                    pcLabel, ( CK_ULONG ) uxLabelLen,
                                                    CKO_PRIVATE_KEY, &xPkHandle );
        }

        if( xResult != CKR_OK )
        {
            lError = -1;
        }
        else
        {
            lError = lPKCS11_initMbedtlsPkContext( pxPkCtx, *pxSessionHandle, xPkHandle );
        }
    }

    return( lError == 0 );
}

BaseType_t xPkcs11WriteCertificate( const char * pcLabel,
                                    mbedtls_x509_crt * pxCertificateContext )
{
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_SESSION_HANDLE xSession = 0;
    CK_RV xResult;

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Token();
    }

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Session( &xSession );
    }

    if( xResult == CKR_OK )
    {
        xResult = xPrvDestoryObject( xSession, CKO_CERTIFICATE, pcLabel );
    }

    if( xResult == CKR_OK )
    {
        int32_t lRslt;
        lRslt = lWriteCertificateToPKCS11( pxCertificateContext,
                                           xSession,
                                           pcLabel,
                                           strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH ) );

        if( lRslt != 0 )
        {
            xResult = CKR_FUNCTION_FAILED;
        }
    }

    if( xSession )
    {
        pxFunctionList->C_CloseSession( xSession );
        xSession = 0;
    }

    return( xResult == CKR_OK );
}

BaseType_t xPkcs11ReadCertificate( mbedtls_x509_crt * pxCertificateContext,
                                   const char * pcCertLabel )
{
    CK_RV xResult;
    CK_SESSION_HANDLE xSession;
    CK_FUNCTION_LIST_PTR pxFunctionList = NULL;
    size_t uxCertLabelLen;
    int32_t lRslt = 0;

    configASSERT( pxCertificateContext );
    configASSERT( pcCertLabel );

    uxCertLabelLen = strnlen( pcCertLabel, pkcs11configMAX_LABEL_LENGTH );

    mbedtls_x509_crt_init( pxCertificateContext );

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Token();
    }

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Session( &xSession );
    }

    if( xResult == CKR_OK )
    {
        lRslt = lReadCertificateFromPKCS11( pxCertificateContext, xSession, pcCertLabel, uxCertLabelLen );
    }

    if( xSession )
    {
        pxFunctionList->C_CloseSession( xSession );
    }

    return( xResult == CKR_OK );
}

BaseType_t xPkcs11GenerateKeyPairEC( char * pcPrivateKeyLabel,
                                     char * pcPublicKeyLabel,
                                     unsigned char ** ppucPublicKeyDer,
                                     size_t * puxPublicKeyDerLen )
{
    CK_RV xResult;
    CK_SESSION_HANDLE xSession = 0;
    CK_OBJECT_HANDLE xPrvKeyHandle = 0;
    CK_OBJECT_HANDLE xPubKeyHandle = 0;
    CK_MECHANISM xMechanism =
    {
        CKM_EC_KEY_PAIR_GEN, NULL_PTR, 0
    };
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_BYTE xEcParams[] = pkcs11DER_ENCODED_OID_P256; /* prime256v1 */
    CK_KEY_TYPE xKeyType = CKK_EC;
    CK_BBOOL xTrue = CK_TRUE;
    CK_ATTRIBUTE xPrivateKeyTemplate[] =
    {
        { CKA_KEY_TYPE, &xKeyType,         sizeof( xKeyType )             },
        { CKA_TOKEN,    &xTrue,            sizeof( xTrue )                },
        { CKA_PRIVATE,  &xTrue,            sizeof( xTrue )                },
        { CKA_SIGN,     &xTrue,            sizeof( xTrue )                },
        { CKA_LABEL,    pcPrivateKeyLabel, strnlen( pcPrivateKeyLabel, pkcs11configMAX_LABEL_LENGTH )}
    };

    CK_ATTRIBUTE xPublicKeyTemplate[] =
    {
        { CKA_KEY_TYPE,  &xKeyType,        sizeof( xKeyType )             },
        { CKA_VERIFY,    &xTrue,           sizeof( xTrue )                },
        { CKA_EC_PARAMS, xEcParams,        sizeof( xEcParams )            },
        { CKA_LABEL,     pcPublicKeyLabel, strnlen( pcPublicKeyLabel, pkcs11configMAX_LABEL_LENGTH )}
    };

    uint32_t ulPubKeyLen = 0;

    configASSERT( pcPrivateKeyLabel );
    configASSERT( pcPublicKeyLabel );
    configASSERT( ppucPublicKeyDer );
    configASSERT( puxPublicKeyDerLen );

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Token();
    }

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Session( &xSession );
    }

    if( xResult == CKR_OK )
    {
        xResult = xPrvDestoryObject( xSession, CKO_PRIVATE_KEY, pcPrivateKeyLabel );
    }

    if( xResult == CKR_OK )
    {
        xResult = xPrvDestoryObject( xSession, CKO_PUBLIC_KEY, pcPublicKeyLabel );
    }

    if( xResult == CKR_OK )
    {
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

    return( xResult == CKR_OK );
}

BaseType_t xPkcs11ExportPublicKey( char * pcPubKeyLabel,
                                   unsigned char ** ppucPublicKeyDer,
                                   size_t * puxPubKeyDerLen )
{
    CK_RV xResult;
    CK_SESSION_HANDLE xSession = 0;
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_OBJECT_HANDLE xPubKeyHandle = 0;
    size_t xPubKeyLabelLen;
    uint32_t ulPubKeyLen = 0;

    configASSERT( pcPubKeyLabel );

    xPubKeyLabelLen = strnlen( pcPubKeyLabel, pkcs11configMAX_LABEL_LENGTH );

    xResult = C_GetFunctionList( &pxFunctionList );

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Token();
    }

    if( xResult == CKR_OK )
    {
        xResult = xInitializePkcs11Session( &xSession );
    }

    if( xResult == CKR_OK )
    {
        xResult = xFindObjectWithLabelAndClass( xSession,
                                                pcPubKeyLabel,
                                                xPubKeyLabelLen,
                                                CKO_PUBLIC_KEY,
                                                &xPubKeyHandle );
    }

    if( xResult == CKR_OK )
    {
        xResult = xPrvExportPubKeyDer( xSession,
                                       xPubKeyHandle,
                                       ppucPublicKeyDer, &ulPubKeyLen );
    }

    if( xResult == CKR_OK )
    {
        *puxPubKeyDerLen = ulPubKeyLen;
    }

    if( xSession )
    {
        xResult = pxFunctionList->C_CloseSession( xSession );
    }

    return( xResult == CKR_OK );
}

#endif /* MBEDTLS_TRANSPORT_PKCS11 */
