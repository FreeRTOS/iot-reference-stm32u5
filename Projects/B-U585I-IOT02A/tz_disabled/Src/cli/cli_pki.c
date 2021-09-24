/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2020-2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

/* FreeRTOS */
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "message_buffer.h"
#include "task.h"

/* Project Specific */
#include "cli.h"
#include "logging.h"
#include "kvstore.h"

/* Standard Lib */
#include <string.h>
#include <stdlib.h>

/* PKCS11 */
#include "pkcs11.h"
#include "core_pkcs11_config.h"
#include "core_pkcs11.h"

/* Mbedtls */
#include "mbedtls/pem.h"
#include "mbedtls/base64.h"
#include "mbedtls/x509_csr.h"
#include "mbedtls/x509.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/pk_internal.h"
#include "core_pki_utils.h"


#define LABEL_PRV_IDX             3
#define LABEL_PUB_IDX             4


typedef struct
{
    CK_FUNCTION_LIST_PTR pxP11FunctionList;
    CK_SESSION_HANDLE xP11Session;
    CK_OBJECT_HANDLE xP11PrivateKey;
    CK_KEY_TYPE xKeyType;
    mbedtls_entropy_context xEntropyCtx;
    mbedtls_ctr_drbg_context xCtrDrbgCtx;
    mbedtls_pk_context xPkeyCtx;
    mbedtls_pk_info_t xPrivKeyInfo;
} PrivateKeySigningCtx_t;


/* Local static functions */
static void vCommand_PKI( ConsoleIO_t * pxCIO, uint32_t ulArgc, char * ppcArgv[] );

const CLI_Command_Definition_t xCommandDef_pki =
{
    .pcCommand = "pki",
    .pcHelpString =
        "pki:\r\n"
        "    Perform public/private key operations on a PKCS11 interface.\r\n"
        "    Usage:\r\n"
        "    pki <verb> <object> <args>\r\n"
        "        Valid verbs are { generate, import, export, list }\r\n"
        "        Valid object types are { key, csr, cert }\r\n"
        "        Arguments should be specified in --<arg_name> <value>\r\n\n"
        "    pki generate key <label_private> <label_public> <algorithm> <algorithm_param>\r\n"
        "        Generates a new private key to be stored in the specified labels\r\n\n"
        "    pki generate csr <label_private>\r\n"
        "        Generates a new Certificate Signing Request using the private key\r\n"
        "        with the specified label.\r\n"
        "        If no label is specified, the default tls private key is used.\r\n\n"
/*        "    pki generate cert <slot>\r\n"
          "        Generate a new self-signed certificate"
          "        -- Not yet implemented --\r\n\n" */
        "    pki import cert <label>\r\n"
        "        Import a certificate into the given slot. The certificate should be \r\n"
        "        copied into the terminal in PEM format, ending with two blank lines.\r\n\n",
/*        "    pki import key <label>\r\n"
          "        -- Not yet implemented --\r\n\n"
          "    pki export cert <label>\r\n"
          "        -- Not yet implemented --\r\n\n"
          "    pki export key <label>\r\n"
          "        Export the public portion of the key with the specified label.\r\n\n"
          "    pki list\r\n"
          "        List objects stored in the pkcs11 keystore.\r\n"
          "        -- Not yet implemented --\r\n\n", */
    .pxCommandInterpreter = vCommand_PKI
};


/* Print a pem file, replacing \n with \r\n */
static void vPrintPem( ConsoleIO_t * pxCIO, const char * pcPem )
{
    size_t xLen = strlen( pcPem );
    for( size_t i = 0; i < xLen; i++ )
    {
        if( pcPem[ i ] == '\n' &&
            ( i == 0 ||
              pcPem[ i - 1 ] != '\r' ) )
        {
            pxCIO->write( "\r\n", 2 );
        }
        else
        {
            pxCIO->write( &( pcPem[i] ), 1 );
        }
    }
}


static int privateKeySigningCallback( void * pvContext,
                                      mbedtls_md_type_t xMdAlg,
                                      const unsigned char * pucHash,
                                      size_t xHashLen,
                                      unsigned char * pucSig,
                                      size_t * pxSigLen,
                                      int ( * piRng )( void *,
                                                       unsigned char *,
                                                       size_t ),
                                      void * pvRng )
{
    CK_RV xResult = CKR_OK;
    int32_t lFinalResult = 0;
    PrivateKeySigningCtx_t * pxCtx = ( PrivateKeySigningCtx_t * ) pvContext;
    CK_MECHANISM xMech = { 0 };
    CK_BYTE xToBeSigned[ 256 ];
    CK_ULONG xToBeSignedLen = sizeof( xToBeSigned );

    /* Unreferenced parameters. */
    ( void ) ( piRng );
    ( void ) ( pvRng );
    ( void ) ( xMdAlg );

    /* Sanity check buffer length. */
    if( xHashLen > sizeof( xToBeSigned ) )
    {
        xResult = CKR_ARGUMENTS_BAD;
    }

    /* Format the hash data to be signed. */
    if( CKK_RSA == pxCtx->xKeyType )
    {
        xMech.mechanism = CKM_RSA_PKCS;

        /* mbedTLS expects hashed data without padding, but PKCS #11 C_Sign function performs a hash
         * & sign if hash algorithm is specified.  This helper function applies padding
         * indicating data was hashed with SHA-256 while still allowing pre-hashed data to
         * be provided. */
        xResult = vAppendSHA256AlgorithmIdentifierSequence( ( uint8_t * ) pucHash, xToBeSigned );
        xToBeSignedLen = pkcs11RSA_SIGNATURE_INPUT_LENGTH;
    }
    else if( CKK_EC == pxCtx->xKeyType )
    {
        xMech.mechanism = CKM_ECDSA;
        ( void ) memcpy( xToBeSigned, pucHash, xHashLen );
        xToBeSignedLen = xHashLen;
    }
    else
    {
        xResult = CKR_ARGUMENTS_BAD;
    }

    if( CKR_OK == xResult )
    {
        /* Use the PKCS#11 module to sign. */
        xResult = pxCtx->pxP11FunctionList->C_SignInit( pxCtx->xP11Session,
                                                        &xMech,
                                                        pxCtx->xP11PrivateKey );
    }

    if( CKR_OK == xResult )
    {
        *pxSigLen = sizeof( xToBeSigned );
        xResult = pxCtx->pxP11FunctionList->C_Sign( ( CK_SESSION_HANDLE ) pxCtx->xP11Session,
                                                     xToBeSigned,
                                                     xToBeSignedLen,
                                                     pucSig,
                                                     ( CK_ULONG_PTR ) pxSigLen );
    }

    if( ( xResult == CKR_OK ) && ( CKK_EC == pxCtx->xKeyType ) )
    {
        /* PKCS #11 for P256 returns a 64-byte signature with 32 bytes for R and 32 bytes for S.
         * This must be converted to an ASN.1 encoded array. */
        if( *pxSigLen != pkcs11ECDSA_P256_SIGNATURE_LENGTH )
        {
            xResult = CKR_FUNCTION_FAILED;
        }

        if( xResult == CKR_OK )
        {
            xResult = PKI_pkcs11SignatureTombedTLSSignature( pucSig, pxSigLen );
        }
    }

    if( xResult != CKR_OK )
    {
        LogError( "Failed to sign message using PKCS #11 with error code %02X.", xResult );
    }

    return lFinalResult;
}

static CK_RV validatePrivateKeyPKCS11( PrivateKeySigningCtx_t * pxCtx,
                                       const char * pcLabel )
{
    CK_RV xResult = CKR_OK;
    CK_SLOT_ID * pxSlotIds = NULL;
    CK_ULONG xCount = 0;
    CK_ATTRIBUTE xTemplate[ 2 ];
    mbedtls_pk_type_t xKeyAlgo = ( mbedtls_pk_type_t ) ~0;

    configASSERT( pxCtx != NULL );
    configASSERT( pcLabel != NULL );

    /* Get the PKCS #11 module/token slot count. */
    if( CKR_OK == xResult )
    {
        xResult = ( BaseType_t ) pxCtx->pxP11FunctionList->C_GetSlotList( CK_TRUE,
                                                                          NULL,
                                                                          &xCount );
    }

    /* Allocate memory to store the token slots. */
    if( CKR_OK == xResult )
    {
        pxSlotIds = ( CK_SLOT_ID * ) pvPortMalloc( sizeof( CK_SLOT_ID ) * xCount );

        if( NULL == pxSlotIds )
        {
            xResult = CKR_HOST_MEMORY;
        }
    }

    /* Get all of the available private key slot identities. */
    if( CKR_OK == xResult )
    {
        xResult = ( BaseType_t ) pxCtx->pxP11FunctionList->C_GetSlotList( CK_TRUE,
                                                                          pxSlotIds,
                                                                          &xCount );
    }

    /* Put the module in authenticated mode. */
    if( CKR_OK == xResult )
    {
        xResult = ( BaseType_t ) pxCtx->pxP11FunctionList->C_Login( pxCtx->xP11Session,
                                                                    CKU_USER,
                                                                    ( CK_UTF8CHAR_PTR ) pkcs11configPKCS11_DEFAULT_USER_PIN,
                                                                    sizeof( pkcs11configPKCS11_DEFAULT_USER_PIN ) - 1 );
    }

    if( CKR_OK == xResult )
    {
        /* Get the handle of the device private key. */
        xResult = xFindObjectWithLabelAndClass( pxCtx->xP11Session,
                                                pcLabel,
                                                strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH - 1 ),
                                                CKO_PRIVATE_KEY,
                                                &pxCtx->xP11PrivateKey );
    }

    if( ( CKR_OK == xResult ) && ( pxCtx->xP11PrivateKey == CK_INVALID_HANDLE ) )
    {
        xResult = CK_INVALID_HANDLE;
        LogError( "Could not find private key with label: %s", pcLabel );
    }

    /* Query the device private key type. */
    if( xResult == CKR_OK )
    {
        xTemplate[ 0 ].type = CKA_KEY_TYPE;
        xTemplate[ 0 ].pValue = &pxCtx->xKeyType;
        xTemplate[ 0 ].ulValueLen = sizeof( CK_KEY_TYPE );
        xResult = pxCtx->pxP11FunctionList->C_GetAttributeValue( pxCtx->xP11Session,
                                                                 pxCtx->xP11PrivateKey,
                                                                 xTemplate,
                                                                 1 );
    }

    /* Map the PKCS #11 key type to an mbedTLS algorithm. */
    if( xResult == CKR_OK )
    {
        switch( pxCtx->xKeyType )
        {
            case CKK_RSA:
                xKeyAlgo = MBEDTLS_PK_RSA;
                break;

            case CKK_EC:
                xKeyAlgo = MBEDTLS_PK_ECKEY;
                break;

            default:
                xResult = CKR_ATTRIBUTE_VALUE_INVALID;
                break;
        }
    }

    /* Map the mbedTLS algorithm to its internal metadata. */
    if( xResult == CKR_OK )
    {
        ( void ) memcpy( &pxCtx->xPrivKeyInfo,
                         mbedtls_pk_info_from_type( xKeyAlgo ),
                         sizeof( mbedtls_pk_info_t ) );

        pxCtx->xPrivKeyInfo.sign_func = privateKeySigningCallback;

        pxCtx->xPkeyCtx.pk_info = &pxCtx->xPrivKeyInfo;
        pxCtx->xPkeyCtx.pk_ctx = pxCtx;
    }

    /* Free memory. */
    vPortFree( pxSlotIds );

    return xResult;
}

#define CSR_BUFFER_LEN 2048

static void vSubCommand_GenerateCsr( ConsoleIO_t * pxCIO, uint32_t ulArgc, char * ppcArgv[] )
{
    const char * pcPrvKeyLabel = pkcs11_TLS_KEY_PRV_LABEL;
    char * pcCsrBuffer = pvPortMalloc( CSR_BUFFER_LEN );
    PrivateKeySigningCtx_t xPksCtx = { 0 };

    int mbedtlsError;

    mbedtls_x509write_csr xCsr;

    if( ulArgc > LABEL_PRV_IDX &&
        ppcArgv[ LABEL_PRV_IDX ] != NULL )
    {
        pcPrvKeyLabel = ppcArgv[ LABEL_PRV_IDX ];
    }

    /* Set the mutex functions for mbed TLS thread safety. */
    mbedtls_threading_set_alt( mbedtls_platform_mutex_init,
                               mbedtls_platform_mutex_free,
                               mbedtls_platform_mutex_lock,
                               mbedtls_platform_mutex_unlock );

    mbedtls_x509write_csr_init( &xCsr );
    mbedtls_pk_init( &( xPksCtx.xPkeyCtx ) );
    mbedtls_entropy_init( &( xPksCtx.xEntropyCtx ) );
    mbedtls_ctr_drbg_init( &( xPksCtx.xCtrDrbgCtx ) );

    mbedtlsError = mbedtls_ctr_drbg_seed( &( xPksCtx.xCtrDrbgCtx ),
                                          mbedtls_entropy_func,
                                          &( xPksCtx.xEntropyCtx ),
                                          NULL,
                                          0 );

    if( mbedtlsError == 0 &&
        strlen( pcPrvKeyLabel ) > 0 )
    {
        CK_RV xResult = C_GetFunctionList( &( xPksCtx.pxP11FunctionList ) );

        if( xResult == CKR_OK )
        {
            xResult = validatePrivateKeyPKCS11( &xPksCtx, pcPrvKeyLabel );
        }

        if( xResult != CKR_OK )
        {
            pxCIO->print( "Error: Unable to validate PKCS11 key.\r\n" );
            mbedtlsError = -1;
        }
    }
    else
    {
        mbedtlsError = -1;
    }

    /* Check if struct was fully initialized */
    if( mbedtlsError == 0 &&
        xPksCtx.xPkeyCtx.pk_ctx == &xPksCtx )
    {
        static const char * pcSubjectNamePrefix = "CN = ";
        size_t xSubjectNameLen = KVStore_getSize( CS_CORE_THING_NAME ) + strlen( pcSubjectNamePrefix );
        char * pcSubjectName = pvPortMalloc( xSubjectNameLen );

        if( pcSubjectName != NULL )
        {
            size_t xIdx = strlcpy( pcSubjectName, pcSubjectNamePrefix, xSubjectNameLen );
            ( void ) KVStore_getString( CS_CORE_THING_NAME,
                                        &( pcSubjectName[ xIdx ] ),
                                        xSubjectNameLen - xIdx );
        }

        mbedtlsError = mbedtls_x509write_csr_set_subject_name( &xCsr, pcSubjectName );


        configASSERT( mbedtlsError == 0 );

        mbedtlsError = mbedtls_x509write_csr_set_key_usage( &xCsr, MBEDTLS_X509_KU_DIGITAL_SIGNATURE );

        configASSERT( mbedtlsError == 0 );

        mbedtlsError = mbedtls_x509write_csr_set_ns_cert_type( &xCsr, MBEDTLS_X509_NS_CERT_TYPE_SSL_CLIENT );

        configASSERT( mbedtlsError == 0 );

        mbedtls_x509write_csr_set_md_alg( &xCsr, MBEDTLS_MD_SHA256 );

        /* Initialize key */
        mbedtls_x509write_csr_set_key( &xCsr, &( xPksCtx.xPkeyCtx ) );


        mbedtlsError = mbedtls_x509write_csr_pem( &xCsr,
                                                  ( unsigned char * ) pcCsrBuffer,
                                                  CSR_BUFFER_LEN,
                                                  mbedtls_ctr_drbg_random,
                                                  &( xPksCtx.xCtrDrbgCtx ) );

        configASSERT( mbedtlsError == 0 );

        /* Cleanup / free memory */
        mbedtls_x509write_csr_free( &xCsr );
        mbedtls_pk_free( &( xPksCtx.xPkeyCtx ) );
        mbedtls_ctr_drbg_free( &( xPksCtx.xCtrDrbgCtx ) );
        mbedtls_entropy_free( &( xPksCtx.xEntropyCtx ) );
        vPortFree( pcSubjectName );
    }

    if( mbedtlsError == 0 )
    {
        vPrintPem( pxCIO, pcCsrBuffer );
    }

    vPortFree( pcCsrBuffer );

}

static CK_RV xGenerateKeyPairEC( CK_SESSION_HANDLE xSession,
                                 uint8_t * pucPrivateKeyLabel,
                                 uint8_t * pucPublicKeyLabel,
                                 CK_OBJECT_HANDLE_PTR pxPrivateKeyHandle,
                                 CK_OBJECT_HANDLE_PTR pxPublicKeyHandle )
{
    CK_RV xResult;
    CK_MECHANISM xMechanism =
    {
        CKM_EC_KEY_PAIR_GEN, NULL_PTR, 0
    };
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_BYTE xEcParams[] = pkcs11DER_ENCODED_OID_P256; /* prime256v1 */
    CK_KEY_TYPE xKeyType = CKK_EC;

    CK_BBOOL xTrue = CK_TRUE;
    CK_ATTRIBUTE xPublicKeyTemplate[] =
    {
        { CKA_KEY_TYPE,  NULL /* &xKeyType */, sizeof( xKeyType )                           },
        { CKA_VERIFY,    NULL /* &xTrue */,    sizeof( xTrue )                              },
        { CKA_EC_PARAMS, NULL /* xEcParams */, sizeof( xEcParams )                          },
        { CKA_LABEL,     pucPublicKeyLabel,    strlen( ( const char * ) pucPublicKeyLabel ) }
    };

    /* Aggregate initializers must not use the address of an automatic variable. */
    /* See MSVC Compiler Warning C4221 */
    xPublicKeyTemplate[ 0 ].pValue = &xKeyType;
    xPublicKeyTemplate[ 1 ].pValue = &xTrue;
    xPublicKeyTemplate[ 2 ].pValue = &xEcParams;

    CK_ATTRIBUTE xPrivateKeyTemplate[] =
    {
        { CKA_KEY_TYPE, &xKeyType,          sizeof( xKeyType )                            },
        { CKA_TOKEN,    &xTrue,             sizeof( xTrue )                               },
        { CKA_PRIVATE,  &xTrue,             sizeof( xTrue )                               },
        { CKA_SIGN,     &xTrue,             sizeof( xTrue )                               },
        { CKA_LABEL,    pucPrivateKeyLabel, strlen( ( const char * ) pucPrivateKeyLabel ) }
    };

    /* Aggregate initializers must not use the address of an automatic variable. */
    /* See MSVC Compiler Warning C4221 */
    xPrivateKeyTemplate[ 0 ].pValue = &xKeyType;
    xPrivateKeyTemplate[ 1 ].pValue = &xTrue;
    xPrivateKeyTemplate[ 2 ].pValue = &xTrue;
    xPrivateKeyTemplate[ 3 ].pValue = &xTrue;

    xResult = C_GetFunctionList( &pxFunctionList );

    xResult = pxFunctionList->C_GenerateKeyPair( xSession,
                                                 &xMechanism,
                                                 xPublicKeyTemplate,
                                                 sizeof( xPublicKeyTemplate ) / sizeof( CK_ATTRIBUTE ),
                                                 xPrivateKeyTemplate, sizeof( xPrivateKeyTemplate ) / sizeof( CK_ATTRIBUTE ),
                                                 pxPublicKeyHandle,
                                                 pxPrivateKeyHandle );
    return xResult;
}


/*
 * @brief Convert a give DER format public key to PEM format
 * @param[in] pcPubKeyDer Public key In DER format
 * @param[in] ulPubKeyDerLen Length of given
 */
static char * pcConvertECDerToPem( uint8_t * pcPubKeyDer, uint32_t ulPubKeyDerLen )
{
    static const char ecPubKeyHeader[] = "-----BEGIN PUBLIC KEY-----\r\n";
    static const char ecPubKeyFooter[] = "-----END PUBLIC KEY-----\r\n";
    int lMbedResult = 0;

    char * pcPubKeyPem = NULL;

    size_t xPubKeyPemLen = 0;

    /* Determine the size of the necessary buffer */
    lMbedResult = mbedtls_pem_write_buffer( ecPubKeyHeader,
                                            ecPubKeyFooter,
                                            pcPubKeyDer,
                                            ulPubKeyDerLen,
                                            NULL,
                                            0,
                                            &xPubKeyPemLen );

    /* Allocate the necessary buffer */
    if( lMbedResult == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL )
    {
        pcPubKeyPem = pvPortMalloc( xPubKeyPemLen );
    }

    if( pcPubKeyPem != NULL )
    {
        lMbedResult = mbedtls_pem_write_buffer( ecPubKeyHeader,
                                                ecPubKeyFooter,
                                                pcPubKeyDer,
                                                ulPubKeyDerLen,
                                                ( unsigned char * ) pcPubKeyPem,
                                                xPubKeyPemLen,
                                                &xPubKeyPemLen );
    }

    /* If the call failed, free the buffer and set the pointer to NULL */
    if( lMbedResult != 0 )
    {
        vPortFree( pcPubKeyPem );
        pcPubKeyPem = NULL;
    }

    return pcPubKeyPem;
}


static char * vExportPubKeyPem( CK_SESSION_HANDLE xSession,
                                CK_OBJECT_HANDLE xPublicKeyHandle )
{
    CK_RV xResult;
    /* Query the key size. */
    CK_ATTRIBUTE xTemplate = { 0 };
    CK_FUNCTION_LIST_PTR pxFunctionList;
    uint8_t * pucPubKeyDer = NULL;
    char * pcPubKeyPem = NULL;
    uint32_t ulDerPublicKeyLength;

    const uint8_t pucEcP256AsnAndOid[] =
    {
        0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
        0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
        0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
        0x42, 0x00
    };

    const uint8_t pucUnusedKeyTag[] = { 0x04, 0x41 };

    xResult = C_GetFunctionList( &pxFunctionList );

    /* Query public key size */

    xTemplate.type = CKA_EC_POINT;
    xTemplate.ulValueLen = 0;
    xTemplate.pValue = NULL;
    xResult = pxFunctionList->C_GetAttributeValue( xSession,
                                                  xPublicKeyHandle,
                                                  &xTemplate,
                                                  1 );

    if( CKR_OK == xResult )
    {
        /* Add space for DER Header */
        xTemplate.ulValueLen += sizeof( pucEcP256AsnAndOid ) - sizeof( pucUnusedKeyTag );
        ulDerPublicKeyLength = xTemplate.ulValueLen;

        /* Allocate a buffer for the DER form  of the key */
        pucPubKeyDer = pvPortMalloc( xTemplate.ulValueLen );
        xResult = CKR_FUNCTION_FAILED;
    }

    /* Read public key into buffer */
    if( pucPubKeyDer != NULL )
    {
        LogInfo( "Allocated %ld bytes for DER form public key.", ulDerPublicKeyLength );

        /* Copy DER header */
        ( void ) memcpy( pucPubKeyDer, pucEcP256AsnAndOid, sizeof( pucEcP256AsnAndOid ) );


        xTemplate.pValue = pucPubKeyDer + sizeof( pucEcP256AsnAndOid ) - sizeof( pucUnusedKeyTag );

        xTemplate.ulValueLen -= ( sizeof( pucEcP256AsnAndOid ) - sizeof( pucUnusedKeyTag ) );

        xResult = pxFunctionList->C_GetAttributeValue( xSession,
                                                       xPublicKeyHandle,
                                                       &xTemplate,
                                                       1 );
    }
    else
    {
        LogError( "Failed to allocate %ld bytes for DER form public key.", ulDerPublicKeyLength );
    }

    /* Convert to PEM */
    if( CKR_OK == xResult )
    {
        pcPubKeyPem = pcConvertECDerToPem( pucPubKeyDer, ulDerPublicKeyLength );
    }

    /* Free the DER key buffer */
    if( pucPubKeyDer != NULL )
    {
        vPortFree( pucPubKeyDer );
        pucPubKeyDer = NULL;
    }

    /* Return the PEM encoded public key buffer */
    return pcPubKeyPem;
}


static void vSubCommand_GenerateKey( ConsoleIO_t * pxCIO, uint32_t ulArgc, char * ppcArgv[] )
{
    CK_RV xResult;
    CK_SESSION_HANDLE xSession = 0;
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_OBJECT_HANDLE xPrvKeyHandle = 0;
    CK_OBJECT_HANDLE xPubKeyHandle = 0;
    BaseType_t xSessionInitialized = pdFALSE;
    char * pcPublicKeyPem = NULL;

    char * pcPrvKeyLabel = pkcs11_TLS_KEY_PRV_LABEL;
    char * pcPubKeyLabel = pkcs11_TLS_KEY_PUB_LABEL;

    if( ulArgc > LABEL_PUB_IDX &&
        ppcArgv[ LABEL_PRV_IDX ] != NULL &&
        ppcArgv[ LABEL_PUB_IDX ] != NULL )
    {
        pcPrvKeyLabel = ppcArgv[ LABEL_PRV_IDX ];
        pcPubKeyLabel = ppcArgv[ LABEL_PUB_IDX ];
    }

    /* Set the mutex functions for mbed TLS thread safety. */
    mbedtls_threading_set_alt( mbedtls_platform_mutex_init,
                               mbedtls_platform_mutex_free,
                               mbedtls_platform_mutex_lock,
                               mbedtls_platform_mutex_unlock );

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
        xSessionInitialized = pdTRUE;
        xResult = xGenerateKeyPairEC( xSession,
                                      ( uint8_t * ) pcPrvKeyLabel,
                                      ( uint8_t * ) pcPubKeyLabel,
                                      &xPrvKeyHandle,
                                      &xPubKeyHandle );
    }



    /* If successful, print public key in PEM form to terminal. */
    if( xResult == CKR_OK )
    {
        pxCIO->print( "SUCCESS: Key pair generated and stored in\r\n" );
        pxCIO->print( "Private Key Label: " );
        pxCIO->print( pkcs11_TLS_KEY_PRV_LABEL );
        pxCIO->print( "\r\nPublic Key Label: " );
        pxCIO->print( pkcs11_TLS_KEY_PUB_LABEL );
        pxCIO->print( "\r\n" );

        pcPublicKeyPem = vExportPubKeyPem( xSession,
                                           xPubKeyHandle );
    }
    else
    {
        pxCIO->print("ERROR: Failed to generate public/private key pair.\r\n");
    }

    /* Print PEM public key */
    if( pcPublicKeyPem != NULL )
    {
        vPrintPem( pxCIO, pcPublicKeyPem );
        pxCIO->print( pcPublicKeyPem );
        /* Free heap allocated memory */
        vPortFree( pcPublicKeyPem );
        pcPublicKeyPem = NULL;
    }

    if( xSessionInitialized == pdTRUE )
    {
        pxFunctionList->C_CloseSession( xSession );
    }
}

static void vSubCommand_GenerateCertificate( ConsoleIO_t * pxCIO, uint32_t ulArgc, char * ppcArgv[] )
{
    pxCIO->print("Not Implemented\r\n");
}

static void vSubCommand_ImportCertificate( ConsoleIO_t * pxCIO, uint32_t ulArgc, char * ppcArgv[] )
{
    pxCIO->print("Not Implemented\r\n");
}

static void vSubCommand_ImportKey( ConsoleIO_t * pxCIO, uint32_t ulArgc, char * ppcArgv[] )
{
    pxCIO->print("Not Implemented\r\n");
}

static void vSubCommand_ExportCertificate( ConsoleIO_t * pxCIO, uint32_t ulArgc, char * ppcArgv[] )
{
    pxCIO->print("Not Implemented\r\n");
}

static void vSubCommand_ExportKey( ConsoleIO_t * pxCIO, uint32_t ulArgc, char * ppcArgv[] )
{
    pxCIO->print("Not Implemented\r\n");
}

#define VERB_ARG_INDEX          1
#define OBJECT_TYPE_INDEX       2

/*
 * CLI format:
 * Argc   1    2            3
 * Idx    0    1            2
 *        pki  generate     key
 *        pki  generate     csr
 *        pki  import       cert
 */
static void vCommand_PKI( ConsoleIO_t * pxCIO,
                          uint32_t ulArgc,
                          char * ppcArgv[] )
{
    const char * pcVerb = NULL;

    BaseType_t xSuccess = pdFALSE;

    if( ulArgc > VERB_ARG_INDEX )
    {
        pcVerb = ppcArgv[ VERB_ARG_INDEX ];

        if( 0 == strcmp( "generate", pcVerb ) )
        {
            if( ulArgc > OBJECT_TYPE_INDEX )
            {
                const char * pcObject = ppcArgv[ OBJECT_TYPE_INDEX ];
                if( 0 == strcmp( "key", pcObject ) )
                {
                    vSubCommand_GenerateKey( pxCIO, ulArgc, ppcArgv );
                    xSuccess = pdTRUE;
                }
                else if( 0 == strcmp( "csr", pcObject ) )
                {
                    vSubCommand_GenerateCsr( pxCIO, ulArgc, ppcArgv );
                    xSuccess = pdTRUE;
                }
                else if( 0 == strcmp( "cert", pcObject ) )
                {
                    vSubCommand_GenerateCertificate( pxCIO, ulArgc, ppcArgv );
                }
                else
                {
                    pxCIO->print( "Error: Invalid object type: '");
                    pxCIO->print( pcObject );
                    pxCIO->print( "' specified for generate command.\r\n" );
                    xSuccess = pdFALSE;
                }
            }
            else
            {
                pxCIO->print( "Error: Not enough arguments to 'pki generate' command.\r\n" );
                xSuccess = pdFALSE;
            }
        }
        else if( 0 == strcmp( "import", pcVerb ) )
        {
            if( ulArgc > OBJECT_TYPE_INDEX )
            {
                const char * pcObject = ppcArgv[ OBJECT_TYPE_INDEX ];
                if( 0 == strcmp( "key", pcObject ) )
                {
                    vSubCommand_ImportKey( pxCIO, ulArgc, ppcArgv );
                    xSuccess = pdTRUE;
                }
                else if( 0 == strcmp( "cert", pcVerb ) )
                {
                    vSubCommand_ImportCertificate( pxCIO, ulArgc, ppcArgv );
                }
                else
                {
                    pxCIO->print( "Error: Invalid object type: '");
                    pxCIO->print( pcObject );
                    pxCIO->print( "' specified for import command.\r\n" );
                    xSuccess = pdFALSE;
                }
            }
            xSuccess = pdFALSE;
        }
        else if( 0 == strcmp( "export", pcVerb ) )
        {
            if( ulArgc > OBJECT_TYPE_INDEX )
            {
                const char * pcObject = ppcArgv[ OBJECT_TYPE_INDEX ];
                if( 0 == strcmp( "key", pcObject ) )
                {
                    vSubCommand_ExportKey( pxCIO, ulArgc, ppcArgv );
                    xSuccess = pdTRUE;
                }
                else if( 0 == strcmp( "cert", pcVerb ) )
                {
                    vSubCommand_ExportCertificate( pxCIO, ulArgc, ppcArgv );
                    xSuccess = pdTRUE;
                }
                else
                {
                    pxCIO->print( "Error: Invalid object type: '");
                    pxCIO->print( pcObject );
                    pxCIO->print( "' specified for import command.\r\n" );
                    xSuccess = pdFALSE;
                }
            }
            xSuccess = pdFALSE;
        }
        else if( 0 == strcmp( "list", pcVerb ) )
        {
            xSuccess = pdFALSE;
        }
    }


    if( xSuccess == pdFALSE )
    {
        pxCIO->print( xCommandDef_pki.pcHelpString );
    }
}
