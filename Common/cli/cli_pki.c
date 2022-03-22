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

#define MBEDTLS_ALLOW_PRIVATE_ACCESS


/* FreeRTOS */
#include "FreeRTOS.h"
#include "message_buffer.h"
#include "task.h"


/* Project Specific */
#include "cli.h"
#include "cli_prv.h"
#include "logging.h"
#include "kvstore.h"

/* Standard Lib */
#include <string.h>
#include <stdlib.h>

#include "tls_transport_config.h"

#if defined( MBEDTLS_TRANSPORT_PKCS11 )

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
#include "mbedtls/pk.h"
#include "pk_wrap.h"
#include "mbedtls/ecp.h"
#include "core_pki_utils.h"


#define LABEL_PUB_IDX          3
#define LABEL_PRV_IDX          4
#define LABEL_IDX              LABEL_PUB_IDX
#define PEM_CERT_MAX_LEN       2048

#define PEM_LINE_ENDING        "\n"
#define PEM_LINE_ENDING_LEN    1


#define PEM_BEGIN              "-----BEGIN "
#define PEM_END                "-----END "
#define PEM_META_SUFFIX        "-----"

/* The first three fields of this struct must match mbedtls_ecp_keypair */
typedef struct
{
    mbedtls_ecp_group grp; /*!<  Elliptic curve and base point     */
    mbedtls_mpi d;         /*!<  our secret value, Empty for this use case */
    mbedtls_ecp_point Q;   /*!<  our public value                  */
    mbedtls_entropy_context xEntropyCtx;
    mbedtls_ctr_drbg_context xCtrDrbgCtx;
    mbedtls_pk_context xPkeyCtx;
    mbedtls_pk_info_t xPrivKeyInfo;
    CK_FUNCTION_LIST_PTR pxP11FunctionList;
    CK_SESSION_HANDLE xP11Session;
    CK_OBJECT_HANDLE xP11PrivateKey;
    CK_KEY_TYPE xKeyType;
} PrivateKeySigningCtx_t;


/* Local static functions */
static void vCommand_PKI( ConsoleIO_t * pxCIO,
                          uint32_t ulArgc,
                          char * ppcArgv[] );
static CK_RV xExportPubKeyDer( CK_SESSION_HANDLE xSession,
                               CK_OBJECT_HANDLE xPublicKeyHandle,
                               uint8_t ** ppucPubKeyDer,
                               uint32_t * pulPubKeyDerLen );

static char * pcExportPubKeyPem( CK_SESSION_HANDLE xSession,
                                 CK_OBJECT_HANDLE xPublicKeyHandle );


const CLI_Command_Definition_t xCommandDef_pki =
{
    .pcCommand            = "pki",
    .pcHelpString         =
        "pki:\r\n"
        "    Perform public/private key operations on a PKCS11 interface.\r\n"
        "    Usage:\r\n"
        "    pki <verb> <object> <args>\r\n"
        "        Valid verbs are { generate, import, export, list }\r\n"
        "        Valid object types are { key, csr, cert }\r\n"
        "        Arguments should be specified in --<arg_name> <value>\r\n\n"
        "    pki generate key <label_public> <label_private> <algorithm> <algorithm_param>\r\n"
        "        Generates a new private key to be stored in the specified labels\r\n\n"
        "    pki generate csr <label>\r\n"
        "        Generates a new Certificate Signing Request using the private key\r\n"
        "        with the specified label.\r\n"
        "        If no label is specified, the default tls private key is used.\r\n\n"

/*        "    pki generate cert <slot>\r\n"
 *         "        Generate a new self-signed certificate"
 *         "        -- Not yet implemented --\r\n\n" */
        "    pki import cert <label>\r\n"
        "        Import a certificate into the given slot. The certificate should be \r\n"
        "        copied into the terminal in PEM format, ending with two blank lines.\r\n\n"

/*        "    pki import key <label>\r\n"
 *        "        -- Not yet implemented --\r\n\n" */
        "    pki export cert <label>\r\n"
        "        Export the certificate with the given label in pem format.\r\n"
        "        When no label is specified, the default certificate is exported.\r\n\n"
        "    pki export key <label>\r\n"
        "        Export the public portion of the key with the specified label.\r\n\n",

/*          "    pki list\r\n"
 *        "        List objects stored in the pkcs11 keystore.\r\n"
 *        "        -- Not yet implemented --\r\n\n", */
    .pxCommandInterpreter = vCommand_PKI
};


/* Print a pem file, replacing \n with \r\n */
static void vPrintPem( ConsoleIO_t * pxCIO,
                       const char * pcPem )
{
    size_t xLen = strlen( pcPem );

    for( size_t i = 0; i < xLen; i++ )
    {
        if( ( pcPem[ i ] == '\n' ) &&
            ( ( i == 0 ) ||
              ( pcPem[ i - 1 ] != '\r' ) ) )
        {
            pxCIO->write( "\r\n", 2 );
        }
        else
        {
            pxCIO->write( &( pcPem[ i ] ), 1 );
        }
    }
}



static int privateKeySigningCallback( void * pvContext,
                                      mbedtls_md_type_t xMdAlg,
                                      const unsigned char * pucHash,
                                      size_t xHashLen,
                                      unsigned char * pucSig,
                                      size_t xSigBufferLen,
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
                                                strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH ),
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

    /*TODO: Support RSA keys here. */
    if( xResult == CKR_OK )
    {
        unsigned char * pucPubKeyDer = NULL;
        uint32_t ulDerPublicKeyLength = 0;

        /* Determine size of key */
        xResult = xExportPubKeyDer( pxCtx->xP11Session,
                                    pxCtx->xP11PrivateKey,
                                    &pucPubKeyDer,
                                    &ulDerPublicKeyLength );

        if( xResult == CKR_OK )
        {
            mbedtls_pk_context xPubPkCtx;

            mbedtls_pk_init( &xPubPkCtx );

            unsigned char * pucPubKeyDerTemp = pucPubKeyDer;
            int lRslt = mbedtls_pk_parse_subpubkey( &pucPubKeyDerTemp,
                                                    &( pucPubKeyDer[ ulDerPublicKeyLength - 1 ] ),
                                                    &xPubPkCtx );

            if( lRslt == 0 )
            {
                /* Copy mbedtls_pk_info_t function pointer struct */
                memcpy( &( pxCtx->xPrivKeyInfo ), xPubPkCtx.pk_info, sizeof( mbedtls_pk_info_t ) );
                /* Copy contents of mbedtls_ecp_keypair struct allocated by mbedtls_pk_parse_subpubkey */
                memcpy( pxCtx, xPubPkCtx.pk_ctx, sizeof( mbedtls_ecp_keypair ) );
            }
            else
            {
                xResult = CKR_PUBLIC_KEY_INVALID;
            }

            vPortFree( pucPubKeyDer );
            pucPubKeyDer = NULL;
        }

        /* Override the signing callback */
        pxCtx->xPrivKeyInfo.sign_func = privateKeySigningCallback;

        pxCtx->xPkeyCtx.pk_info = &( pxCtx->xPrivKeyInfo );
        pxCtx->xPkeyCtx.pk_ctx = pxCtx;
    }

    /* Free memory. */
    vPortFree( pxSlotIds );

    return xResult;
}

#define CSR_BUFFER_LEN    2048

static void vSubCommand_GenerateCsr( ConsoleIO_t * pxCIO,
                                     uint32_t ulArgc,
                                     char * ppcArgv[] )
{
    const char * pcPrvKeyLabel = pkcs11_TLS_KEY_PRV_LABEL;
    char * pcCsrBuffer = pvPortMalloc( CSR_BUFFER_LEN );
    PrivateKeySigningCtx_t xPksCtx = { 0 };
    BaseType_t xSessionInitialized = pdFALSE;

    int mbedtlsError;

    mbedtls_x509write_csr xCsr;

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        pcPrvKeyLabel = ppcArgv[ LABEL_IDX ];
    }

    /* Set the mutex functions for mbed TLS thread safety. */
/*    mbedtls_threading_set_alt( mbedtls_platform_mutex_init, */
/*                               mbedtls_platform_mutex_free, */
/*                               mbedtls_platform_mutex_lock, */
/*                               mbedtls_platform_mutex_unlock ); */

    mbedtls_x509write_csr_init( &xCsr );
    mbedtls_pk_init( &( xPksCtx.xPkeyCtx ) );
    mbedtls_entropy_init( &( xPksCtx.xEntropyCtx ) );
    mbedtls_ctr_drbg_init( &( xPksCtx.xCtrDrbgCtx ) );

    mbedtlsError = mbedtls_ctr_drbg_seed( &( xPksCtx.xCtrDrbgCtx ),
                                          mbedtls_entropy_func,
                                          &( xPksCtx.xEntropyCtx ),
                                          NULL,
                                          0 );

    if( ( mbedtlsError == 0 ) &&
        ( strlen( pcPrvKeyLabel ) > 0 ) )
    {
        CK_RV xResult = C_GetFunctionList( &( xPksCtx.pxP11FunctionList ) );

        if( xResult == CKR_OK )
        {
            xResult = xInitializePkcs11Token();
        }

        if( xResult == CKR_OK )
        {
            xResult = xInitializePkcs11Session( &( xPksCtx.xP11Session ) );
        }

        if( xResult == CKR_OK )
        {
            xSessionInitialized = pdTRUE;
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
    if( mbedtlsError == 0 )
    {
        static const char * pcSubjectNamePrefix = "CN=";
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
        mbedtls_ctr_drbg_free( &( xPksCtx.xCtrDrbgCtx ) );
        mbedtls_entropy_free( &( xPksCtx.xEntropyCtx ) );

        if( pcSubjectName != NULL )
        {
            vPortFree( pcSubjectName );
        }
    }

    if( mbedtlsError == 0 )
    {
        vPrintPem( pxCIO, pcCsrBuffer );
    }

    vPortFree( pcCsrBuffer );

    if( xSessionInitialized == pdTRUE )
    {
        xPksCtx.pxP11FunctionList->C_CloseSession( xPksCtx.xP11Session );
    }
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
        { CKA_KEY_TYPE,  &xKeyType,         sizeof( xKeyType )                           },
        { CKA_VERIFY,    &xTrue,            sizeof( xTrue )                              },
        { CKA_EC_PARAMS, xEcParams,         sizeof( xEcParams )                          },
        { CKA_LABEL,     pucPublicKeyLabel, strlen( ( const char * ) pucPublicKeyLabel ) }
    };

    CK_ATTRIBUTE xPrivateKeyTemplate[] =
    {
        { CKA_KEY_TYPE, &xKeyType,          sizeof( xKeyType )                            },
        { CKA_TOKEN,    &xTrue,             sizeof( xTrue )                               },
        { CKA_PRIVATE,  &xTrue,             sizeof( xTrue )                               },
        { CKA_SIGN,     &xTrue,             sizeof( xTrue )                               },
        { CKA_LABEL,    pucPrivateKeyLabel, strlen( ( const char * ) pucPrivateKeyLabel ) }
    };

    xResult = C_GetFunctionList( &pxFunctionList );

    xResult = pxFunctionList->C_GenerateKeyPair( xSession,
                                                 &xMechanism,
                                                 &( xPublicKeyTemplate[ 0 ] ),
                                                 sizeof( xPublicKeyTemplate ) / sizeof( CK_ATTRIBUTE ),
                                                 &( xPrivateKeyTemplate[ 0 ] ),
                                                 sizeof( xPrivateKeyTemplate ) / sizeof( CK_ATTRIBUTE ),
                                                 pxPublicKeyHandle,
                                                 pxPrivateKeyHandle );
    return xResult;
}


/*
 * @brief Convert a give DER format public key to PEM format
 * @param[in] pcPubKeyDer Public key In DER format
 * @param[in] ulPubKeyDerLen Length of given
 */
static char * pcConvertECDerToPem( uint8_t * pcPubKeyDer,
                                   uint32_t ulPubKeyDerLen )
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

/*TODO: implement CKA_PUBLIC_KEY_INFO on the backend to make this compliant with the standard and reduce unnecessary memory allocation / deallocation. */
/* Caller must free the returned buffer */
static CK_RV xExportPubKeyDer( CK_SESSION_HANDLE xSession,
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

static char * pcExportPubKeyPem( CK_SESSION_HANDLE xSession,
                                 CK_OBJECT_HANDLE xPublicKeyHandle )
{
    CK_RV xResult;
    uint32_t ulDerPublicKeyLength;
    uint8_t * pucPubKeyDer = NULL;
    char * pcPubKeyPem = NULL;

    xResult = xExportPubKeyDer( xSession,
                                xPublicKeyHandle,
                                &pucPubKeyDer,
                                &ulDerPublicKeyLength );

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


static void vSubCommand_GenerateKey( ConsoleIO_t * pxCIO,
                                     uint32_t ulArgc,
                                     char * ppcArgv[] )
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

    if( ( ulArgc > LABEL_PRV_IDX ) &&
        ( ppcArgv[ LABEL_PRV_IDX ] != NULL ) &&
        ( ppcArgv[ LABEL_PUB_IDX ] != NULL ) )
    {
        pcPrvKeyLabel = ppcArgv[ LABEL_PRV_IDX ];
        pcPubKeyLabel = ppcArgv[ LABEL_PUB_IDX ];
    }

    /* Set the mutex functions for mbed TLS thread safety. */
    /* mbedtls_threading_set_alt( mbedtls_platform_mutex_init, */
    /*                            mbedtls_platform_mutex_free, */
    /*                            mbedtls_platform_mutex_lock, */
    /*                            mbedtls_platform_mutex_unlock ); */

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

        pcPublicKeyPem = pcExportPubKeyPem( xSession,
                                            xPubKeyHandle );
    }
    else
    {
        pxCIO->print( "ERROR: Failed to generate public/private key pair.\r\n" );
    }

    /* Print PEM public key */
    if( pcPublicKeyPem != NULL )
    {
        vPrintPem( pxCIO, pcPublicKeyPem );
        /* Free heap allocated memory */
        vPortFree( pcPublicKeyPem );
        pcPublicKeyPem = NULL;
    }

    if( xSessionInitialized == pdTRUE )
    {
        pxFunctionList->C_CloseSession( xSession );
    }
}

static void vSubCommand_GenerateCertificate( ConsoleIO_t * pxCIO,
                                             uint32_t ulArgc,
                                             char * ppcArgv[] )
{
    pxCIO->print( "Not Implemented\r\n" );
}

static BaseType_t xImportCertificateIntoP11( const char * pcLabel,
                                             const char * pcCertificate,
                                             uint32_t ulCertLen )
{
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_SESSION_HANDLE xSession = 0;
    CK_RV xResult;
    BaseType_t xSessionInitialized = pdFALSE;
    CK_OBJECT_HANDLE xCertHandle = 0;

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

        /* Look for an existing object that we may need to overwrite */
        xResult = xFindObjectWithLabelAndClass( xSession,
                                                pcLabel,
                                                strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH ),
                                                CKO_CERTIFICATE,
                                                &xCertHandle );
    }

    if( ( xResult == CKR_OK ) &&
        ( xCertHandle != CK_INVALID_HANDLE ) )
    {
        xResult = pxFunctionList->C_DestroyObject( xSession,
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
                .pValue = &xObjClass
            },
            {
                .type = CKA_LABEL,
                .ulValueLen = strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH ),
                .pValue = pcLabel
            },
            {
                .type = CKA_CERTIFICATE_TYPE,
                .ulValueLen = sizeof( CK_CERTIFICATE_TYPE ),
                .pValue = &xCertType
            },
            {
                .type = CKA_TOKEN,
                .ulValueLen = sizeof( CK_BBOOL ),
                .pValue = &xPersistCert
            },
            {
                .type = CKA_VALUE,
                .ulValueLen = ulCertLen,
                .pValue = pcCertificate
            }
        };

        xResult = pxFunctionList->C_CreateObject( xSession,
                                                  pxTemplate,
                                                  5,
                                                  &xCertHandle );
    }

    if( xSessionInitialized == pdTRUE )
    {
        pxFunctionList->C_CloseSession( xSession );
    }

    return( xResult == CKR_OK );
}

static inline size_t xCopyLine( char * const pcDest,
                                const char * pcSrc,
                                uint32_t xDestLen )
{
    size_t xLen;

    xLen = strlcpy( pcDest,
                    pcSrc,
                    xDestLen );

    configASSERT( xLen < xDestLen );

    xLen += strlcpy( &( pcDest[ xLen ] ),
                     PEM_LINE_ENDING,
                     xDestLen - xLen );

    configASSERT( xLen < xDestLen );

    return xLen;
}

static size_t xReadPemFromCLI( ConsoleIO_t * pxCIO,
                               char * const pcBuffer,
                               const uint32_t xBufferLen )
{
    size_t xPemDataLen = 0;
    BaseType_t xBeginFound = pdFALSE;
    BaseType_t xEndFound = pdFALSE;

    while( xPemDataLen < xBufferLen &&
           xEndFound == pdFALSE )
    {
        char * pcInputBuffer = NULL;
        int32_t lDataRead = pxCIO->readline( &pcInputBuffer );
        BaseType_t xErrorFlag = pdFALSE;

        if( lDataRead > 0 )
        {
            if( lDataRead > 64 )
            {
                pxCIO->print( "Error: Current line exceeds maximum line length for a PEM file.\r\n" );
                xErrorFlag = pdTRUE;
            }
            /* Check if this line will overflow the buffer given for the pem file */
            else if( ( xErrorFlag == pdFALSE ) &&
                     ( lDataRead > 0 ) &&
                     ( ( xPemDataLen + lDataRead + PEM_LINE_ENDING_LEN ) >= xBufferLen ) )
            {
                pxCIO->print( "Error: Out of memory to store the given PEM file.\r\n" );
                xErrorFlag = pdTRUE;
            }
            /* Validate a header header line */
            else if( xBeginFound == pdFALSE )
            {
                if( strncmp( PEM_BEGIN, pcInputBuffer, strlen( PEM_BEGIN ) ) == 0 )
                {
                    char * pcLabelEnd = strnstr( &( pcInputBuffer[ strlen( PEM_BEGIN ) ] ),
                                                 PEM_META_SUFFIX,
                                                 lDataRead - strlen( PEM_BEGIN ) );

                    if( pcLabelEnd == NULL )
                    {
                        pxCIO->print( "Error: PEM header does not contain the expected ending: '" PEM_META_SUFFIX "'.\r\n" );
                        xErrorFlag = pdTRUE;
                    }
                    else
                    {
                        xBeginFound = pdTRUE;
                    }
                }
                else
                {
                    pxCIO->print( "Error: PEM header does not contain the expected text: '" PEM_BEGIN "'.\r\n" );
                    xErrorFlag = pdTRUE;
                }
            }
            else if( xEndFound == pdFALSE )
            {
                if( strncmp( PEM_END, pcInputBuffer, strlen( PEM_END ) ) == 0 )
                {
                    char * pcLabelEnd = strnstr( &( pcInputBuffer[ strlen( PEM_END ) ] ),
                                                 PEM_META_SUFFIX,
                                                 lDataRead - strlen( PEM_END ) );

                    if( pcLabelEnd == NULL )
                    {
                        pxCIO->print( "Error: PEM footer does not contain the expected ending: '" PEM_META_SUFFIX "'.\r\n" );
                        xErrorFlag = pdTRUE;
                    }
                    else
                    {
                        xEndFound = pdTRUE;
                    }
                }
            }
            else
            {
                /* Empty */
            }

            if( xBeginFound == pdTRUE )
            {
                xPemDataLen += xCopyLine( &( pcBuffer[ xPemDataLen ] ),
                                          pcInputBuffer,
                                          xBufferLen - xPemDataLen );

                configASSERT( xPemDataLen < xBufferLen );
            }
        }

        if( xErrorFlag == pdTRUE )
        {
            xPemDataLen = 0;
            break;
        }
    }

    /* Add NULL terminator */
    pcBuffer[ xPemDataLen ] = '\0';

    return xPemDataLen;
}


static void vSubCommand_ImportCertificate( ConsoleIO_t * pxCIO,
                                           uint32_t ulArgc,
                                           char * ppcArgv[] )
{
    BaseType_t xResult = pdTRUE;

    char pcCertLabel[ pkcs11configMAX_LABEL_LENGTH ] = { 0 };
    size_t xCertLabelLen = 0;

    char * pcCertData = NULL;
    size_t xCertDataLen = 0;


    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        ( void ) strlcpy( pcCertLabel, ppcArgv[ LABEL_IDX ], pkcs11configMAX_LABEL_LENGTH );
    }
    else
    {
        ( void ) strlcpy( pcCertLabel, pkcs11_TLS_CERT_LABEL, pkcs11configMAX_LABEL_LENGTH );
    }

    xCertLabelLen = strnlen( pcCertLabel, pkcs11configMAX_LABEL_LENGTH );

    if( xCertLabelLen > pkcs11configMAX_LABEL_LENGTH )
    {
        pxCIO->print( "Error: Certificate label: '" );
        pxCIO->print( pcCertLabel );
        pxCIO->print( "' is longer than the configured maximum length.\r\n" );
        xResult = pdFALSE;
    }

    if( xResult == pdTRUE )
    {
        pcCertData = pvPortMalloc( PEM_CERT_MAX_LEN + 1 );
        ( void ) memset( pcCertData, 0, PEM_CERT_MAX_LEN + 1 );
    }

    if( pcCertData == NULL )
    {
        pxCIO->print( "Error: Failed to allocate #PEM_CERT_MAX_LEN bytes to store the given certificate.\r\n" );
        xResult = pdFALSE;
    }
    else
    {
        xCertDataLen = xReadPemFromCLI( pxCIO, pcCertData, PEM_CERT_MAX_LEN );
    }

    /*TODO: parse received pem with mbedtls to verify. */

    if( xCertDataLen > 0 )
    {
        xResult = xImportCertificateIntoP11( pcCertLabel, pcCertData, xCertDataLen + 1 );
    }
    else
    {
        LogDebug( "xReadPemFromCLI returned %ld", xCertDataLen );
        xResult = pdFALSE;
    }

    if( xResult == pdTRUE )
    {
        pxCIO->print( "Success: Certificate loaded to label: '" );
        pxCIO->print( pcCertLabel );
        pxCIO->print( "'.\r\n" );
    }
    else
    {
        pxCIO->print( "Error: failed to save certificate to label: '" );
        pxCIO->print( pcCertLabel );
        pxCIO->print( "'.\r\n" );
    }
}

static void vSubCommand_ImportKey( ConsoleIO_t * pxCIO,
                                   uint32_t ulArgc,
                                   char * ppcArgv[] )
{
    pxCIO->print( "Not Implemented\r\n" );
}

static void vSubCommand_ExportCertificate( ConsoleIO_t * pxCIO,
                                           uint32_t ulArgc,
                                           char * ppcArgv[] )
{
    CK_RV xResult;
    CK_SESSION_HANDLE xSession = 0;
    CK_FUNCTION_LIST_PTR pxFunctionList;
    CK_OBJECT_HANDLE xCertHandle = 0;
    BaseType_t xSessionInitialized = pdFALSE;

    char * pcCertLabel = pkcs11_TLS_CERT_LABEL;
    size_t xCertLabelLen;

    char * pcCertPem = NULL;

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        pcCertLabel = ppcArgv[ LABEL_PUB_IDX ];
    }

    xCertLabelLen = strnlen( pcCertLabel, pkcs11configMAX_LABEL_LENGTH );

    /* Set the mutex functions for mbed TLS thread safety. */
    /* mbedtls_threading_set_alt( mbedtls_platform_mutex_init, */
    /*                            mbedtls_platform_mutex_free, */
    /*                            mbedtls_platform_mutex_lock, */
    /*                            mbedtls_platform_mutex_unlock ); */

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
        xResult = xFindObjectWithLabelAndClass( xSession,
                                                pcCertLabel,
                                                xCertLabelLen,
                                                CKO_CERTIFICATE,
                                                &xCertHandle );
    }

    if( ( xResult != CKR_OK ) ||
        ( xCertHandle == CK_INVALID_HANDLE ) )
    {
        pxCIO->print( "ERROR: Failed to locate certificate with label: '" );
        pxCIO->write( pcCertLabel, xCertLabelLen );
        pxCIO->print( "'\r\n" );
    }
    /* If successful, print certificate in PEM form to terminal. */
    else
    {
        pxCIO->print( "Certificate Label: " );
        pxCIO->print( pcCertLabel );
        pxCIO->print( "\r\n" );

        CK_ATTRIBUTE xTemplate =
        {
            .type       = CKA_VALUE,
            .ulValueLen = 0,
            .pValue     = NULL
        };

        /* Determine the required buffer length */
        xResult = pxFunctionList->C_GetAttributeValue( xSession,
                                                       xCertHandle,
                                                       &xTemplate,
                                                       1 );

        pcCertPem = pvPortMalloc( xTemplate.ulValueLen );

        if( pcCertPem != NULL )
        {
            xTemplate.pValue = pcCertPem;

            xResult = pxFunctionList->C_GetAttributeValue( xSession,
                                                           xCertHandle,
                                                           &xTemplate,
                                                           1 );
        }
    }

    if( ( pcCertPem != NULL ) &&
        ( xResult == CKR_OK ) )
    {
        vPrintPem( pxCIO, pcCertPem );

        /* Free heap allocated memory */
        vPortFree( pcCertPem );
        pcCertPem = NULL;
    }

    if( xSessionInitialized == pdTRUE )
    {
        pxFunctionList->C_CloseSession( xSession );
    }
}

static void vSubCommand_ExportKey( ConsoleIO_t * pxCIO,
                                   uint32_t ulArgc,
                                   char * ppcArgv[] )
{
    CK_RV xResult;

    CK_SESSION_HANDLE xSession = 0;

    CK_FUNCTION_LIST_PTR pxFunctionList;

    CK_OBJECT_HANDLE xPubKeyHandle = 0;

    BaseType_t xSessionInitialized = pdFALSE;

    char * pcPublicKeyPem = NULL;
    char * pcPubKeyLabel = pkcs11_TLS_KEY_PUB_LABEL;
    size_t xPubKeyLabelLen;

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        pcPubKeyLabel = ppcArgv[ LABEL_PUB_IDX ];
    }

    xPubKeyLabelLen = strnlen( pcPubKeyLabel, pkcs11configMAX_LABEL_LENGTH );

    /* Set the mutex functions for mbed TLS thread safety. */
    /* mbedtls_threading_set_alt( mbedtls_platform_mutex_init, */
    /*                            mbedtls_platform_mutex_free, */
    /*                            mbedtls_platform_mutex_lock, */
    /*                            mbedtls_platform_mutex_unlock ); */

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
        xResult = xFindObjectWithLabelAndClass( xSession,
                                                pcPubKeyLabel,
                                                xPubKeyLabelLen,
                                                CKO_PUBLIC_KEY,
                                                &xPubKeyHandle );
    }

    /* If successful, print public key in PEM form to terminal. */
    if( xResult == CKR_OK )
    {
        pxCIO->print( "Public Key Label: " );
        pxCIO->write( pcPubKeyLabel, xPubKeyLabelLen );
        pxCIO->print( "\r\n" );

        pcPublicKeyPem = pcExportPubKeyPem( xSession,
                                            xPubKeyHandle );
    }
    else
    {
        pxCIO->print( "ERROR: Failed to locate public key with label: '" );
        pxCIO->write( pcPubKeyLabel, xPubKeyLabelLen );
        pxCIO->print( "'\r\n" );
    }

    /* Print PEM public key */
    if( pcPublicKeyPem != NULL )
    {
        vPrintPem( pxCIO, pcPublicKeyPem );
        /* Free heap allocated memory */
        vPortFree( pcPublicKeyPem );
        pcPublicKeyPem = NULL;
    }

    if( xSessionInitialized == pdTRUE )
    {
        pxFunctionList->C_CloseSession( xSession );
    }
}

#define VERB_ARG_INDEX       1
#define OBJECT_TYPE_INDEX    2

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
                    pxCIO->print( "Error: Invalid object type: '" );
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
                else if( 0 == strcmp( "cert", pcObject ) )
                {
                    vSubCommand_ImportCertificate( pxCIO, ulArgc, ppcArgv );
                    xSuccess = pdTRUE;
                }
                else
                {
                    pxCIO->print( "Error: Invalid object type: '" );
                    pxCIO->print( pcObject );
                    pxCIO->print( "' specified for import command.\r\n" );
                    xSuccess = pdFALSE;
                }
            }
        }
        else if( 0 == strcmp( "export", pcVerb ) )
        {
            xSuccess = pdFALSE;

            if( ulArgc > OBJECT_TYPE_INDEX )
            {
                const char * pcObject = ppcArgv[ OBJECT_TYPE_INDEX ];

                if( 0 == strcmp( "key", pcObject ) )
                {
                    vSubCommand_ExportKey( pxCIO, ulArgc, ppcArgv );
                    xSuccess = pdTRUE;
                }
                else if( 0 == strcmp( "cert", pcObject ) )
                {
                    vSubCommand_ExportCertificate( pxCIO, ulArgc, ppcArgv );
                    xSuccess = pdTRUE;
                }
                else
                {
                    pxCIO->print( "Error: Invalid object type: '" );
                    pxCIO->print( pcObject );
                    pxCIO->print( "' specified for import command.\r\n" );
                    xSuccess = pdFALSE;
                }
            }
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

#endif /* defined( MBEDTLS_TRANSPORT_PKCS11 ) */
