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
#include "PkiObject.h"
#include "PkiObject_prv.h"
#include "mbedtls_error_utils.h"
#include "mbedtls_transport.h"

#include <stdlib.h>
#include "psa/crypto_types.h"
#include "tls_transport_config.h"

#include "mbedtls/platform.h"

#include "ota_config.h"

/*-----------------------------------------------------------*/

PkiStatus_t xPrvMbedtlsErrToPkiStatus( int lError )
{
    PkiStatus_t xStatus;

    switch( lError )
    {
        case 0:
            xStatus = PKI_SUCCESS;
            break;

        case MBEDTLS_ERR_X509_ALLOC_FAILED:
            xStatus = PKI_ERR_NOMEM;
            break;

        case MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED:
            xStatus = PKI_ERR_INTERNAL;
            break;

        case MBEDTLS_ERR_X509_CERT_UNKNOWN_FORMAT:
        case MBEDTLS_ERR_X509_BAD_INPUT_DATA:
        case MBEDTLS_ERR_PEM_BAD_INPUT_DATA:
        case MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT:
            xStatus = PKI_ERR_OBJ_PARSING_FAILED;
            break;

        default:
            xStatus = lError < 0 ? PKI_ERR : PKI_SUCCESS;
            break;
    }

    return xStatus;
}

#if TEST_AUTOMATION_INTEGRATION == 1
char g_CodeSigningCert[] = otapalconfigCODE_SIGNING_CERTIFICATE;
char g_ClientCertificate[] = keyCLIENT_CERTIFICATE_PEM;
char g_ClientPrivateKey[] = keyCLIENT_PRIVATE_KEY_PEM;
char g_CaRootCert[] = keyCA_ROOT_CERT_PEM;
#endif

/*-----------------------------------------------------------*/

PkiObject_t xPkiObjectFromLabel( const char * pcLabel )
{
    PkiObject_t xPkiObject = { 0 };

    if( pcLabel != NULL )
    {
#if TEST_AUTOMATION_INTEGRATION == 1
        if( ( strcmp( OTA_SIGNING_KEY_LABEL, pcLabel ) == 0 ) &&
            ( strlen( g_CodeSigningCert ) > 0 ) )
        {
#if defined( MBEDTLS_TRANSPORT_PSA )
            psa_key_attributes_t xKeyAttrs = { 0 };

            if( psa_get_key_attributes( OTA_SIGNING_KEY_ID, &xKeyAttrs ) != 0 )
            {
                int lError = 0;
                mbedtls_pk_context xPkContext;
                mbedtls_pk_init( &xPkContext );
                lError = mbedtls_pk_parse_public_key( &xPkContext,
                                                      g_CodeSigningCert,
                                                      sizeof( g_CodeSigningCert ) );

                lError = lWritePublicKeyToPSACrypto( OTA_SIGNING_KEY_ID, &xPkContext );
            }

            xPkiObject.xForm = OBJ_FORM_PSA_CRYPTO;
            xPkiObject.xPsaCryptoId = OTA_SIGNING_KEY_ID;
#else /* if defined( MBEDTLS_TRANSPORT_PSA ) */
            xPkiObject.xForm = OBJ_FORM_PEM;
            xPkiObject.uxLen = strlen( g_CodeSigningCert ) + 1;
            xPkiObject.pucBuffer = g_CodeSigningCert;
            /*xPkiObject.xPsaCryptoId = OTA_SIGNING_KEY_ID; */
#endif /* if defined( MBEDTLS_TRANSPORT_PSA ) */
            return xPkiObject;
        }

        if( ( strcmp( TLS_KEY_PRV_LABEL, pcLabel ) == 0 ) &&
            ( strlen( g_ClientPrivateKey ) > 0 ) )
        {
            xPkiObject.xForm = OBJ_FORM_PEM;
            xPkiObject.uxLen = strlen( g_ClientPrivateKey ) + 1;
            xPkiObject.pucBuffer = g_ClientPrivateKey;
            /*xPkiObject.xPsaCryptoId = OTA_SIGNING_KEY_ID; */
            return xPkiObject;
        }

        if( ( strcmp( TLS_CERT_LABEL, pcLabel ) == 0 ) &&
            ( strlen( g_ClientCertificate ) > 0 ) )
        {
            xPkiObject.xForm = OBJ_FORM_PEM;
            xPkiObject.uxLen = strlen( g_ClientCertificate ) + 1;
            xPkiObject.pucBuffer = g_ClientCertificate;
            /*xPkiObject.xPsaCryptoId = OTA_SIGNING_KEY_ID; */
            return xPkiObject;
        }

        if( ( strcmp( TLS_ROOT_CA_CERT_LABEL, pcLabel ) == 0 ) &&
            ( strlen( g_CaRootCert ) > 0 ) )
        {
            xPkiObject.xForm = OBJ_FORM_PEM;
            xPkiObject.uxLen = strlen( g_CaRootCert ) + 1;
            xPkiObject.pucBuffer = g_CaRootCert;
            /*xPkiObject.xPsaCryptoId = OTA_SIGNING_KEY_ID; */
            return xPkiObject;
        }
#endif /* ifdef TEST_AUTOMATION_INTEGRATION */
#if defined( MBEDTLS_TRANSPORT_PKCS11 )
        xPkiObject.pcPkcs11Label = pcLabel;
        xPkiObject.uxLen = strnlen( pcLabel, configTLS_MAX_LABEL_LEN );
        xPkiObject.xForm = OBJ_FORM_PKCS11_LABEL;
#elif defined( MBEDTLS_TRANSPORT_PSA )
        if( strcmp( TLS_KEY_PRV_LABEL, pcLabel ) == 0 )
        {
            xPkiObject.xForm = OBJ_FORM_PSA_CRYPTO;
            xPkiObject.xPsaCryptoId = PSA_TLS_PRV_KEY_ID;
        }
        else if( strcmp( TLS_KEY_PUB_LABEL, pcLabel ) == 0 )
        {
            xPkiObject.xForm = OBJ_FORM_PSA_CRYPTO;
            xPkiObject.xPsaCryptoId = PSA_TLS_PUB_KEY_ID;
        }
        else if( strcmp( TLS_CERT_LABEL, pcLabel ) == 0 )
        {
            xPkiObject.xForm = OBJ_FORM_PSA_PS;
            xPkiObject.xPsaStorageId = PSA_TLS_CERT_ID;
        }
        else if( strcmp( TLS_ROOT_CA_CERT_LABEL, pcLabel ) == 0 )
        {
            xPkiObject.xForm = OBJ_FORM_PSA_PS;
            xPkiObject.xPsaStorageId = PSA_TLS_ROOT_CA_CERT_ID;
        }
        else if( strcmp( OTA_SIGNING_KEY_LABEL, pcLabel ) == 0 )
        {
            xPkiObject.xForm = OBJ_FORM_PSA_CRYPTO;
            xPkiObject.xPsaCryptoId = OTA_SIGNING_KEY_ID;
        }
        else
        {
            xPkiObject.xForm = OBJ_FORM_NONE;
        }
#endif /* if defined( MBEDTLS_TRANSPORT_PKCS11 ) */
    }

    return xPkiObject;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkiReadCertificate( mbedtls_x509_crt * pxMbedtlsCertCtx,
                                 const PkiObject_t * pxCertificate )
{
    PkiStatus_t xStatus = PKI_ERR_OBJ_NOT_FOUND;

    configASSERT( pxMbedtlsCertCtx != NULL );
    configASSERT( pxCertificate != NULL );

    switch( pxCertificate->xForm )
    {
        case OBJ_FORM_PEM:
           {
               int lError = mbedtls_x509_crt_parse( pxMbedtlsCertCtx,
                                                    pxCertificate->pucBuffer,
                                                    pxCertificate->uxLen );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to parse certificate from buffer: 0x%08X, length: %ld",
                                     pxCertificate->pucBuffer, pxCertificate->uxLen );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

        case OBJ_FORM_DER:
           {
               int lError = mbedtls_x509_crt_parse_der( pxMbedtlsCertCtx,
                                                        pxCertificate->pucBuffer,
                                                        pxCertificate->uxLen );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to parse certificate from buffer: 0x%08X, length: %ld,",
                                     pxCertificate->pucBuffer, pxCertificate->uxLen );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

#ifdef MBEDTLS_TRANSPORT_PKCS11
        case OBJ_FORM_PKCS11_LABEL:
            xStatus = xPkcs11ReadCertificate( pxMbedtlsCertCtx, pxCertificate->pcPkcs11Label );
            break;
#endif /* ifdef MBEDTLS_TRANSPORT_PKCS11 */
#ifdef MBEDTLS_TRANSPORT_PSA
        case OBJ_FORM_PSA_CRYPTO:
           {
               int lError = lReadCertificateFromPSACrypto( pxMbedtlsCertCtx,
                                                           pxCertificate->xPsaCryptoId );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to read certificate(s) from PSA Crypto uid: 0x%08X,",
                                     pxCertificate->xPsaCryptoId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

        case OBJ_FORM_PSA_ITS:
           {
               int lError = lReadCertificateFromPsaIts( pxMbedtlsCertCtx,
                                                        pxCertificate->xPsaStorageId );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to read certificate(s) from PSA ITS uid: 0x%016ULLX,",
                                     pxCertificate->xPsaStorageId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

        case OBJ_FORM_PSA_PS:
           {
               int lError = lReadCertificateFromPsaPS( pxMbedtlsCertCtx,
                                                       pxCertificate->xPsaStorageId );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to read certificate(s) from PSA PS uid: 0x%016ULLX,",
                                     pxCertificate->xPsaStorageId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;
#endif /* ifdef MBEDTLS_TRANSPORT_PSA */
        case OBJ_FORM_NONE:
        /* Intentional fall through */
        default:
            LogError( "Invalid certificate form specified." );
            xStatus = PKI_ERR_ARG_INVALID;
            break;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/


PkiStatus_t xPkiWriteCertificate( const char * pcCertLabel,
                                  const mbedtls_x509_crt * pxMbedtlsCertCtx )
{
    PkiStatus_t xStatus = PKI_SUCCESS;
    PkiObject_t xCertObject = { 0 };

    configASSERT( pcCertLabel != NULL );
    configASSERT( pxMbedtlsCertCtx != NULL );

    xCertObject = xPkiObjectFromLabel( pcCertLabel );

    switch( xCertObject.xForm )
    {
        case OBJ_FORM_PEM:
        case OBJ_FORM_DER:
            xStatus = PKI_ERR_ARG_INVALID;
            break;

#ifdef MBEDTLS_TRANSPORT_PKCS11
        case OBJ_FORM_PKCS11_LABEL:
            xStatus = xPkcs11WriteCertificate( xCertObject.pcPkcs11Label, pxMbedtlsCertCtx );
            break;
#endif /* ifdef MBEDTLS_TRANSPORT_PKCS11 */
#ifdef MBEDTLS_TRANSPORT_PSA
        case OBJ_FORM_PSA_CRYPTO:
           {
               int lError = lWriteCertificateToPSACrypto( xCertObject.xPsaCryptoId, pxMbedtlsCertCtx );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to write certificate to PSA Crypto uid: 0x%08X,",
                                     xCertObject.xPsaCryptoId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

        case OBJ_FORM_PSA_ITS:
           {
               int lError = lWriteCertificateToPsaIts( xCertObject.xPsaStorageId,
                                                       pxMbedtlsCertCtx );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to write certificate to PSA ITS uid: 0x%PRIX64,",
                                     xCertObject.xPsaStorageId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

        case OBJ_FORM_PSA_PS:
           {
               int lError = lWriteCertificateToPsaPS( xCertObject.xPsaStorageId, pxMbedtlsCertCtx );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to write certificate to PSA PS uid: 0x%PRIX64,",
                                     xCertObject.xPsaStorageId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;
#endif /* ifdef MBEDTLS_TRANSPORT_PSA */
        case OBJ_FORM_NONE:
        /* Intentional fall through */
        default:
            LogError( "Invalid certificate form specified." );
            xStatus = PKI_ERR_ARG_INVALID;
            break;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkiWritePubKey( const char * pcPubKeyLabel,
                             const unsigned char * pucPubKeyDer,
                             const size_t uxPubKeyDerLen,
                             mbedtls_pk_context * pxPkContext )
{
    PkiStatus_t xStatus = PKI_SUCCESS;
    PkiObject_t xPubKeyObject = { 0 };

    configASSERT( pcPubKeyLabel != NULL );
    configASSERT( pxPkContext != NULL );

    xPubKeyObject = xPkiObjectFromLabel( pcPubKeyLabel );

    switch( xPubKeyObject.xForm )
    {
        case OBJ_FORM_PEM:
        case OBJ_FORM_DER:
            xStatus = PKI_ERR_ARG_INVALID;
            break;

#ifdef MBEDTLS_TRANSPORT_PKCS11
        case OBJ_FORM_PKCS11_LABEL:
            xStatus = xPkcs11WritePubKey( xPubKeyObject.pcPkcs11Label, pxPkContext );
            break;
#endif /* ifdef MBEDTLS_TRANSPORT_PKCS11 */
#ifdef MBEDTLS_TRANSPORT_PSA
        case OBJ_FORM_PSA_CRYPTO:
           {
               int lError = lWritePublicKeyToPSACrypto( xPubKeyObject.xPsaCryptoId, pxPkContext );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to write public key to PSA Crypto uid: 0x%08X,",
                                     xPubKeyObject.xPsaCryptoId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

        case OBJ_FORM_PSA_ITS:
           {
               int lError = lWriteObjectToPsaIts( xPubKeyObject.xPsaStorageId, pucPubKeyDer, uxPubKeyDerLen );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to write public key to PSA ITS uid: 0x%PRIX64,",
                                     xPubKeyObject.xPsaStorageId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

        case OBJ_FORM_PSA_PS:
           {
               int lError = lWriteObjectToPsaPs( xPubKeyObject.xPsaStorageId, pucPubKeyDer, uxPubKeyDerLen );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to write public key to PSA PS uid: 0x%PRIX64,",
                                     xPubKeyObject.xPsaStorageId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;
#endif /* ifdef MBEDTLS_TRANSPORT_PSA */
        case OBJ_FORM_NONE:
        /* Intentional fall through */
        default:
            LogError( "Invalid public key form specified." );
            xStatus = PKI_ERR_ARG_INVALID;
            break;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkiReadPrivateKey( mbedtls_pk_context * pxPkCtx,
                                const PkiObject_t * pxPrivateKey,
                                int ( * pxRngCallback )( void *, unsigned char *, size_t ),
                                void * pvRngCtx )
{
    PkiStatus_t xStatus = PKI_SUCCESS;

    configASSERT( pxPkCtx != NULL );
    configASSERT( pxPrivateKey != NULL );

    switch( pxPrivateKey->xForm )
    {
        case OBJ_FORM_PEM:
        /* Intentional fall through */
        case OBJ_FORM_DER:

            if( ( pxPrivateKey->uxLen == 0 ) ||
                ( pxPrivateKey->pucBuffer == NULL ) )
            {
                xStatus = PKI_ERR_ARG_INVALID;
            }
            else
            {
                int lError = mbedtls_pk_parse_key( pxPkCtx,
                                                   pxPrivateKey->pucBuffer,
                                                   pxPrivateKey->uxLen,
                                                   NULL, 0,
                                                   pxRngCallback,
                                                   pvRngCtx );

                MBEDTLS_LOG_IF_ERROR( lError, "Failed to parse the client key at memory address %p.",
                                      pxPrivateKey->pucBuffer );

                xStatus = xPrvMbedtlsErrToPkiStatus( lError );
            }

            break;

#ifdef MBEDTLS_TRANSPORT_PKCS11
        case OBJ_FORM_PKCS11_LABEL:
            xStatus = xPkcs11InitMbedtlsPkContext( pxPrivateKey->pcPkcs11Label, pxPkCtx, NULL );
            break;
#endif /* ifdef MBEDTLS_TRANSPORT_PKCS11 */

#ifdef MBEDTLS_TRANSPORT_PSA
        case OBJ_FORM_PSA_CRYPTO:
           {
/*                int lError = mbedtls_pk_setup_opaque( pxPkCtx, */
/*                                                      pxPrivateKey->xPsaCryptoId ); */

               int lError = lPsa_initMbedtlsPkContext( pxPkCtx, pxPrivateKey->xPsaCryptoId );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to initialize the PSA opaque key context. ObjectId: 0x%08x",
                                     pxPrivateKey->xPsaCryptoId );

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }

           break;

        case OBJ_FORM_PSA_ITS:
           {
               unsigned char * pucPk = NULL;
               size_t uxPkLen = 0;

               int lError = lReadObjectFromPsaIts( &pucPk, &uxPkLen,
                                                   pxPrivateKey->xPsaStorageId );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to read the private key blob. PSA ITS ObjectId: 0x%ullx.",
                                     pxPrivateKey->xPsaStorageId );

               if( lError == 0 )
               {
                   lError = mbedtls_pk_parse_key( pxPkCtx,
                                                  pucPk, uxPkLen,
                                                  NULL, 0,
                                                  pxRngCallback,
                                                  pvRngCtx );

                   MBEDTLS_LOG_IF_ERROR( lError, "Failed to parse the private key blob. PSA ITS ObjectId: 0x%ullx.",
                                         pxPrivateKey->xPsaStorageId );
               }

               if( pucPk != NULL )
               {
                   configASSERT( uxPkLen > 0 );
                   mbedtls_platform_zeroize( pucPk, uxPkLen );
                   mbedtls_free( pucPk );
               }

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
           break;

        case OBJ_FORM_PSA_PS:
           {
               unsigned char * pucPk = NULL;
               size_t uxPkLen = 0;

               int lError = lReadObjectFromPsaPs( &pucPk, &uxPkLen,
                                                  pxPrivateKey->xPsaStorageId );

               MBEDTLS_LOG_IF_ERROR( lError, "Failed to read the private key blob. PSA PS ObjectId: 0x%ullx.",
                                     pxPrivateKey->xPsaStorageId );

               if( lError == 0 )
               {
                   lError = mbedtls_pk_parse_key( pxPkCtx,
                                                  pucPk, uxPkLen,
                                                  NULL, 0,
                                                  pxRngCallback,
                                                  pvRngCtx );

                   MBEDTLS_LOG_IF_ERROR( lError, "Failed to parse the private key blob. PSA PS ObjectId: 0x%ullx.",
                                         pxPrivateKey->xPsaStorageId );
               }

               if( pucPk != NULL )
               {
                   configASSERT( uxPkLen > 0 );
                   mbedtls_platform_zeroize( pucPk, uxPkLen );
                   mbedtls_free( pucPk );
               }

               xStatus = xPrvMbedtlsErrToPkiStatus( lError );
           }
#endif /* ifdef MBEDTLS_TRANSPORT_PSA */

        case OBJ_FORM_NONE:
        /* Intentional fallthrough */
        default:
            LogError( "Invalid key form specified." );
            xStatus = PKI_ERR_ARG_INVALID;
            break;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkiReadPublicKey( mbedtls_pk_context * pxPkCtx,
                               const PkiObject_t * pxPublicKey )
{
    PkiStatus_t xStatus = PKI_SUCCESS;
    unsigned char * pucPubKeyDer = NULL;
    size_t uxPubKeyLen = 0;

    if( ( pxPkCtx == NULL ) ||
        ( pxPublicKey == NULL ) )
    {
        xStatus = PKI_ERR_ARG_INVALID;
    }
    else
    {
        xStatus = xPkiReadPublicKeyDer( &pucPubKeyDer, &uxPubKeyLen, pxPublicKey );
    }

    if( xStatus == PKI_SUCCESS )
    {
        int lError = 0;

        if( pxPublicKey->xForm == OBJ_FORM_PEM )
        {
            lError = mbedtls_pk_parse_public_key( pxPkCtx,
                                                  pucPubKeyDer,
                                                  uxPubKeyLen );
        }
        else
        {
            unsigned char * pucPk = pucPubKeyDer;
            lError = mbedtls_pk_parse_subpubkey( &pucPk, pucPk + uxPubKeyLen, pxPkCtx );
        }

        MBEDTLS_LOG_IF_ERROR( lError, "Failed to parse the public key at memory address %p.",
                              pxPublicKey->pucBuffer );

        xStatus = xPrvMbedtlsErrToPkiStatus( lError );
    }

    if( pucPubKeyDer != NULL )
    {
        configASSERT( uxPubKeyLen > 0 );
        mbedtls_platform_zeroize( pucPubKeyDer, uxPubKeyLen );
        mbedtls_free( pucPubKeyDer );
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkiReadPublicKeyDer( unsigned char ** ppucPubKeyDer,
                                  size_t * puxPubKeyDerLen,
                                  const PkiObject_t * pxPublicKey )
{
    PkiStatus_t xStatus = PKI_SUCCESS;

    if( ( ppucPubKeyDer == NULL ) ||
        ( puxPubKeyDerLen == NULL ) ||
        ( pxPublicKey == NULL ) )
    {
        xStatus = PKI_ERR_ARG_INVALID;
    }
    else
    {
        switch( pxPublicKey->xForm )
        {
            case OBJ_FORM_PEM:
            /* Intentional fall through */
            case OBJ_FORM_DER:

                if( ( pxPublicKey->uxLen == 0 ) ||
                    ( pxPublicKey->pucBuffer == NULL ) )
                {
                    xStatus = PKI_ERR_ARG_INVALID;
                }
                else
                {
                    void * pvBuf = pvPortMalloc( pxPublicKey->uxLen );
                    memcpy( pvBuf, pxPublicKey->pucBuffer, pxPublicKey->uxLen );

                    *ppucPubKeyDer = ( unsigned char * ) pvBuf;
                    *puxPubKeyDerLen = pxPublicKey->uxLen;
                }

                break;

#ifdef MBEDTLS_TRANSPORT_PKCS11
            case OBJ_FORM_PKCS11_LABEL:
                xStatus = xPkcs11ReadPublicKey( ppucPubKeyDer, puxPubKeyDerLen, pxPublicKey->pcPkcs11Label );

                if( xStatus != PKI_SUCCESS )
                {
                    LogError( "Failed to read public key with label: %s from PKCS11 module.",
                              pxPublicKey->pcPkcs11Label );
                }
                break;
#endif /* ifdef MBEDTLS_TRANSPORT_PKCS11 */

#ifdef MBEDTLS_TRANSPORT_PSA
            case OBJ_FORM_PSA_CRYPTO:
               {
                   int lError = xReadPublicKeyFromPSACrypto( ppucPubKeyDer, puxPubKeyDerLen,
                                                             pxPublicKey->xPsaCryptoId );

                   MBEDTLS_LOG_IF_ERROR( lError, "Failed to read public key object from PSA crypto service. ObjectId: 0x%08x",
                                         pxPublicKey->xPsaCryptoId );

                   xStatus = xPrvMbedtlsErrToPkiStatus( lError );
                   break;
               }

            case OBJ_FORM_PSA_ITS:
               {
                   int lError = lReadObjectFromPsaIts( ppucPubKeyDer, puxPubKeyDerLen,
                                                       pxPublicKey->xPsaStorageId );

                   MBEDTLS_LOG_IF_ERROR( lError, "Failed to read the public key blob. PSA ITS ObjectId: 0x%ullx.",
                                         pxPublicKey->xPsaStorageId );

                   xStatus = xPrvMbedtlsErrToPkiStatus( lError );
                   break;
               }

            case OBJ_FORM_PSA_PS:
               {
                   int lError = lReadObjectFromPsaIts( ppucPubKeyDer, puxPubKeyDerLen,
                                                       pxPublicKey->xPsaStorageId );

                   MBEDTLS_LOG_IF_ERROR( lError, "Failed to read the public key blob. PSA PS ObjectId: 0x%ullx.",
                                         pxPublicKey->xPsaStorageId );

                   xStatus = xPrvMbedtlsErrToPkiStatus( lError );
                   break;
               }
#endif /* ifdef MBEDTLS_TRANSPORT_PSA */

            case OBJ_FORM_NONE:
            /* Intentional fallthrough */
            default:
                LogError( "Invalid key form specified." );
                xStatus = PKI_ERR_ARG_INVALID;
                break;
        }
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

PkiStatus_t xPkiGenerateECKeypair( const char * pcPrvKeyLabel,
                                   const char * pcPubKeyLabel,
                                   unsigned char ** ppucPubKeyDer,
                                   size_t * puxPubKeyDerLen )
{
    PkiStatus_t xStatus = PKI_SUCCESS;
    PkiObject_t xPubKey;
    PkiObject_t xPrvKey;

    if( ( ppucPubKeyDer == NULL ) ||
        ( puxPubKeyDerLen == NULL ) ||
        ( pcPrvKeyLabel == NULL ) ||
        ( pcPubKeyLabel == NULL ) )
    {
        xStatus = PKI_ERR_ARG_INVALID;
    }
    else
    {
        xPrvKey = xPkiObjectFromLabel( pcPrvKeyLabel );
        xPubKey = xPkiObjectFromLabel( pcPubKeyLabel );

        switch( xPubKey.xForm )
        {
            case OBJ_FORM_PEM:
            case OBJ_FORM_DER:
                xStatus = PKI_ERR_ARG_INVALID;
                break;

#ifdef MBEDTLS_TRANSPORT_PKCS11
            case OBJ_FORM_PKCS11_LABEL:
                xStatus = xPkcs11GenerateKeyPairEC( xPrvKey.pcPkcs11Label, xPubKey.pcPkcs11Label, ppucPubKeyDer, puxPubKeyDerLen );
                break;
#endif
#ifdef MBEDTLS_TRANSPORT_PSA
            case OBJ_FORM_PSA_CRYPTO:
               {
                   int lError = lGenerateKeyPairECPsaCrypto( xPrvKey.xPsaCryptoId, xPubKey.xPsaCryptoId, ppucPubKeyDer, puxPubKeyDerLen );
                   xStatus = xPrvMbedtlsErrToPkiStatus( lError );
               }
               break;

            case OBJ_FORM_PSA_ITS:
            case OBJ_FORM_PSA_PS:
                xStatus = PKI_ERR_ARG_INVALID;
                break;
#endif /* ifdef MBEDTLS_TRANSPORT_PSA */
            case OBJ_FORM_NONE:
            /* Intentional fallthrough */
            default:
                LogError( "Invalid key form specified." );
                xStatus = PKI_ERR_ARG_INVALID;
                break;
        }
    }

    return xStatus;
}
