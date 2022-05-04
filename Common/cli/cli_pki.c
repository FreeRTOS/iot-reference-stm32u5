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
#include "mbedtls_transport.h"

#ifdef MBEDTLS_TRANSPORT_PKCS11
/* PKCS11 */
#include "pkcs11.h"
#include "core_pkcs11_config.h"
#include "core_pkcs11.h"
#include "core_pki_utils.h"
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

/* Mbedtls */
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


#define LABEL_PUB_IDX          3
#define LABEL_PRV_IDX          4
#define LABEL_IDX              LABEL_PUB_IDX
#define PEM_CERT_MAX_LEN       2048

#define PEM_LINE_ENDING        "\n"
#define PEM_LINE_ENDING_LEN    1


#define PEM_BEGIN              "-----BEGIN "
#define PEM_END                "-----END "
#define PEM_META_SUFFIX        "-----"

/* Local static functions */
static void vCommand_PKI( ConsoleIO_t * pxCIO,
                          uint32_t ulArgc,
                          char * ppcArgv[] );

const CLI_Command_Definition_t xCommandDef_pki =
{
    .pcCommand            = "pki",
    .pcHelpString         =
        "pki:\r\n"
        "    Perform public/private key operations.\r\n"
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
        "    pki generate cert <cert_label> <private_key_label>\r\n"
        "        Generate a new self-signed certificate\r\n\n"
        "    pki import cert <label>\r\n"
        "        Import a certificate into the given slot. The certificate should be \r\n"
        "        copied into the terminal in PEM format, ending with two blank lines.\r\n\n"
        "    pki export cert <label>\r\n"
        "        Export the certificate with the given label in pem format.\r\n"
        "        When no label is specified, the default certificate is exported.\r\n\n"
        "    pki import key <label>\r\n"
        "        Import a public key into the given slot. The key should be \r\n"
        "        copied into the terminal in PEM format, ending with two blank lines.\r\n\n"
        "    pki export key <label>\r\n"
        "        Export the public portion of the key with the specified label.\r\n\n",
    .pxCommandInterpreter = vCommand_PKI
};


#define PEM_LINE_BUFF_LEN         64U
#define DER_BYTES_PER_PEM_LINE    ( 3U * PEM_LINE_BUFF_LEN / 4U )

static void vPrintDer( ConsoleIO_t * pxCIO,
                       const char * pcHeader,
                       const char * pcFooter,
                       const unsigned char * pucDer,
                       size_t uxDerLen )
{
    char pcLineBuffer[ PEM_LINE_BUFF_LEN + 3 ];
    int32_t lRslt = 0;

    pxCIO->print( pcHeader );

    for( size_t uxIdx = 0; uxIdx < uxDerLen; uxIdx += DER_BYTES_PER_PEM_LINE )
    {
        size_t uxBytesToConsume = DER_BYTES_PER_PEM_LINE;
        size_t uxBytesWritten = 0;

        memset( pcLineBuffer, 0xFF, PEM_LINE_BUFF_LEN + 3 );

        if( ( uxDerLen - uxIdx ) < DER_BYTES_PER_PEM_LINE )
        {
            uxBytesToConsume = uxDerLen - uxIdx;
        }

        /* Convert each 6-bits of data to a single base64 character (48 input bytes per 64 char line) */

        lRslt = mbedtls_base64_encode( ( unsigned char * ) pcLineBuffer,
                                       PEM_LINE_BUFF_LEN + 1,
                                       &uxBytesWritten,
                                       &( pucDer[ uxIdx ] ),
                                       uxBytesToConsume );

        if( lRslt == 0 )
        {
            ( void ) strncpy( &( pcLineBuffer[ uxBytesWritten ] ), "\r\n", 3 );
            uxBytesWritten += 2;
            pxCIO->write( pcLineBuffer, uxBytesWritten );
        }
        else
        {
            pxCIO->print( "\r\nERROR\r\n" );
            break;
        }
    }

    if( lRslt == 0 )
    {
        pxCIO->print( pcFooter );
    }
}


#define CSR_BUFFER_LEN    2048

static void vSubCommand_GenerateCsr( ConsoleIO_t * pxCIO,
                                     uint32_t ulArgc,
                                     char * ppcArgv[] )
{
    PkiStatus_t xStatus = PKI_SUCCESS;
    char * pcPrvKeyLabel = NULL;
    unsigned char * pucCsrDer = NULL;
    size_t uxCsrDerLen = 0;
    mbedtls_pk_context xPkCtx;
    mbedtls_entropy_context xEntropyCtx;
    PkiObject_t xPrvKeyObj;

    int lError = -1;

    pcPrvKeyLabel = TLS_KEY_PRV_LABEL;

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        pcPrvKeyLabel = ppcArgv[ LABEL_IDX ];
    }

    xPrvKeyObj = xPkiObjectFromLabel( pcPrvKeyLabel );

    pucCsrDer = pvPortMalloc( CSR_BUFFER_LEN );
    mbedtls_pk_init( &xPkCtx );
    mbedtls_entropy_init( &xEntropyCtx );

    xStatus = xPkiReadPrivateKey( &xPkCtx, &xPrvKeyObj, mbedtls_entropy_func, &xEntropyCtx );

    if( xStatus == PKI_SUCCESS )
    {
        mbedtls_x509write_csr xCsr;
        static const char * pcSubjectNamePrefix = "CN=";
        size_t xSubjectNameLen = KVStore_getSize( CS_CORE_THING_NAME ) + strlen( pcSubjectNamePrefix );
        char * pcSubjectName = pvPortMalloc( xSubjectNameLen );

        mbedtls_x509write_csr_init( &xCsr );

        if( pcSubjectName != NULL )
        {
            size_t xIdx = strlcpy( pcSubjectName, pcSubjectNamePrefix, xSubjectNameLen );
            ( void ) KVStore_getString( CS_CORE_THING_NAME,
                                        &( pcSubjectName[ xIdx ] ),
                                        xSubjectNameLen - xIdx );
        }

        lError = mbedtls_x509write_csr_set_subject_name( &xCsr, pcSubjectName );
        MBEDTLS_MSG_IF_ERROR( lError, "Failed to set CSR Subject Name: " );

        if( lError >= 0 )
        {
            lError = mbedtls_x509write_csr_set_key_usage( &xCsr, MBEDTLS_X509_KU_DIGITAL_SIGNATURE );
            MBEDTLS_MSG_IF_ERROR( lError, "Failed to set CSR Key Usage: " );
        }

        if( lError >= 0 )
        {
            lError = mbedtls_x509write_csr_set_ns_cert_type( &xCsr, MBEDTLS_X509_NS_CERT_TYPE_SSL_CLIENT );
            MBEDTLS_MSG_IF_ERROR( lError, "Failed to set CSR NS Certificate Type: " );
        }

        if( lError >= 0 )
        {
            mbedtls_x509write_csr_set_md_alg( &xCsr, MBEDTLS_MD_SHA256 );

            mbedtls_x509write_csr_set_key( &xCsr, &xPkCtx );

            lError = mbedtls_x509write_csr_der( &xCsr,
                                                pucCsrDer,
                                                CSR_BUFFER_LEN,
                                                mbedtls_entropy_func, &xEntropyCtx );

            /* mbedtls_x509write_csr_der returns the length of data written to the end of the buffer. */
            if( lError > 0 )
            {
                configASSERT( CSR_BUFFER_LEN > lError );
                uxCsrDerLen = ( size_t ) lError;

                ( void ) memmove( pucCsrDer, &( pucCsrDer[ CSR_BUFFER_LEN - lError ] ), uxCsrDerLen );
                lError = 0;
            }

            MBEDTLS_MSG_IF_ERROR( lError, "Failed to create CSR:" );
        }

        /* Cleanup / free memory */
        mbedtls_x509write_csr_free( &xCsr );

        if( pcSubjectName != NULL )
        {
            vPortFree( pcSubjectName );
            pcSubjectName = NULL;
        }
    }

    if( ( lError >= 0 ) &&
        ( uxCsrDerLen > 0 ) &&
        ( pucCsrDer != NULL ) )
    {
        vPrintDer( pxCIO,
                   "-----BEGIN CERTIFICATE REQUEST-----\r\n",
                   "-----END CERTIFICATE REQUEST-----\r\n",
                   pucCsrDer, uxCsrDerLen );
    }

    if( pucCsrDer != NULL )
    {
        vPortFree( pucCsrDer );
    }

    mbedtls_entropy_free( &xEntropyCtx );

#ifdef MBEDTLS_TRANSPORT_PKCS11
    lError = lPKCS11PkMbedtlsCloseSessionAndFree( &xPkCtx );
#endif /* MBEDTLS_TRANSPORT_PKCS11 */
}

static void vSubCommand_GenerateCertificate( ConsoleIO_t * pxCIO,
                                             uint32_t ulArgc,
                                             char * ppcArgv[] )
{
    PkiStatus_t xResult = PKI_SUCCESS;
    char * pcCertLabel = NULL;
    char * pcPrvKeyLabel = NULL;
    unsigned char * pucCertDer = NULL;
    size_t uxCertDerLen = 0;
    mbedtls_pk_context xPkCtx;
    mbedtls_entropy_context xEntropyCtx;
    PkiObject_t xPrvKeyObject;
    int lError = 0;

    pcCertLabel = TLS_CERT_LABEL;
    pcPrvKeyLabel = TLS_KEY_PRV_LABEL;

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        pcCertLabel = ppcArgv[ LABEL_IDX ];
    }

    if( ( ulArgc > LABEL_PRV_IDX ) &&
        ( ppcArgv[ LABEL_PRV_IDX ] != NULL ) )
    {
        pcPrvKeyLabel = ppcArgv[ LABEL_PRV_IDX ];
    }

    pucCertDer = mbedtls_calloc( 1, CSR_BUFFER_LEN );

    if( !pucCertDer )
    {
        xResult = PKI_ERR_NOMEM;
    }

    xPrvKeyObject = xPkiObjectFromLabel( pcPrvKeyLabel );

    mbedtls_pk_init( &xPkCtx );
    mbedtls_entropy_init( &xEntropyCtx );

    xResult = xPkiReadPrivateKey( &xPkCtx, &xPrvKeyObject, mbedtls_entropy_func, &xEntropyCtx );

    if( xResult == PKI_SUCCESS )
    {
        mbedtls_x509write_cert xWriteCertCtx;

        mbedtls_x509write_crt_init( &xWriteCertCtx );

        /* Set subject and issuer key to the device key */
        mbedtls_x509write_crt_set_subject_key( &xWriteCertCtx, &xPkCtx );
        mbedtls_x509write_crt_set_issuer_key( &xWriteCertCtx, &xPkCtx );

        mbedtls_x509write_crt_set_version( &xWriteCertCtx, MBEDTLS_X509_CRT_VERSION_3 );
        mbedtls_x509write_crt_set_md_alg( &xWriteCertCtx, MBEDTLS_MD_SHA256 );

        lError = mbedtls_x509write_crt_set_ns_cert_type( &xWriteCertCtx, MBEDTLS_X509_NS_CERT_TYPE_SSL_CLIENT );
        MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate NS Cert Type: " );

        if( lError >= 0 )
        {
            mbedtls_mpi xCertSerialNumber;
            mbedtls_mpi_init( &xCertSerialNumber );

            lError = mbedtls_mpi_fill_random( &xCertSerialNumber, sizeof( uint64_t ),
                                              mbedtls_entropy_func, &xEntropyCtx );
            MBEDTLS_MSG_IF_ERROR( lError, "Failed to generate a random certificate serial number: " );

            if( lError >= 0 )
            {
                lError = mbedtls_x509write_crt_set_serial( &xWriteCertCtx, &xCertSerialNumber );
                MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate Serial Number" );
            }
        }

        if( lError >= 0 )
        {
            /* TODO: Determine from date time agent and/or time hwm. */
            lError = mbedtls_x509write_crt_set_validity( &xWriteCertCtx, "19700101000000", "20691231235959" );
            MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate validity range: " );
        }

        if( lError >= 0 )
        {
            static const char * pcSubjectNamePrefix = "CN=";
            char * pcSubjectName = NULL;
            size_t xSubjectNameLen = 0;

            xSubjectNameLen = strlen( pcSubjectNamePrefix ) + KVStore_getSize( CS_CORE_THING_NAME );

            if( xSubjectNameLen > strlen( pcSubjectNamePrefix ) )
            {
                pcSubjectName = pvPortMalloc( xSubjectNameLen );
            }

            if( pcSubjectName == NULL )
            {
                lError = MBEDTLS_ERR_X509_ALLOC_FAILED;
                LogError( "Failed to allocate memory to store subject name." );
            }
            else
            {
                size_t xIdx = strlcpy( pcSubjectName, pcSubjectNamePrefix, xSubjectNameLen );
                ( void ) KVStore_getString( CS_CORE_THING_NAME,
                                            &( pcSubjectName[ xIdx ] ),
                                            xSubjectNameLen - xIdx );

                lError = mbedtls_x509write_crt_set_subject_name( &xWriteCertCtx, pcSubjectName );
                MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate Subject Name: " );
            }

            if( lError >= 0 )
            {
                lError = mbedtls_x509write_crt_set_issuer_name( &xWriteCertCtx, pcSubjectName );
                MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate Issuer Name: " );
            }

            if( pcSubjectName )
            {
                vPortFree( pcSubjectName );
                pcSubjectName = NULL;
            }
        }

        if( lError >= 0 )
        {
            lError = mbedtls_x509write_crt_set_basic_constraints( &xWriteCertCtx, 0, 0 );
            MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate Basic Constraints: " );
        }

        if( lError >= 0 )
        {
            lError = mbedtls_x509write_crt_set_subject_key_identifier( &xWriteCertCtx );
            MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate Subject Key Identifier: " );
        }

        if( lError >= 0 )
        {
            lError = mbedtls_x509write_crt_set_authority_key_identifier( &xWriteCertCtx );
            MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate Authority Key Identifier: " );
        }

        if( lError >= 0 )
        {
            lError = mbedtls_x509write_crt_set_key_usage( &xWriteCertCtx, MBEDTLS_X509_KU_DIGITAL_SIGNATURE );
            MBEDTLS_MSG_IF_ERROR( lError, "Failed to set Certificate Key Usage: " );
        }

        if( lError >= 0 )
        {
            lError = mbedtls_x509write_crt_der( &xWriteCertCtx,
                                                pucCertDer,
                                                CSR_BUFFER_LEN,
                                                mbedtls_entropy_func, &xEntropyCtx );

            if( lError > 0 )
            {
                configASSERT( CSR_BUFFER_LEN > lError );
                uxCertDerLen = ( size_t ) lError;

                ( void ) memmove( pucCertDer, &( pucCertDer[ CSR_BUFFER_LEN - lError ] ), uxCertDerLen );
                lError = 0;
            }

            MBEDTLS_MSG_IF_ERROR( lError, "Failed to create certificate:" );
        }

        /* Cleanup / free memory */
        mbedtls_x509write_crt_free( &xWriteCertCtx );
    }

    if( ( lError >= 0 ) &&
        ( uxCertDerLen > 0 ) &&
        ( pucCertDer != NULL ) )
    {
        mbedtls_x509_crt xCertContext;
        mbedtls_x509_crt_init( &xCertContext );

        lError = mbedtls_x509_crt_parse_der_nocopy( &xCertContext,
                                                    pucCertDer,
                                                    uxCertDerLen );

        MBEDTLS_MSG_IF_ERROR( lError, "Failed to validate resulting certificate." );

        if( lError >= 0 )
        {
/*            PkiObject_t xCert = xPkiObjectFromLabel( pcCertLabel ); */
            xCertContext.MBEDTLS_PRIVATE( own_buffer ) = 1;

            xResult = xPkiWriteCertificate( pcCertLabel, &xCertContext );

            vPrintDer( pxCIO,
                       "-----BEGIN CERTIFICATE-----\r\n",
                       "-----END CERTIFICATE-----\r\n",
                       pucCertDer, uxCertDerLen );
        }

        mbedtls_x509_crt_free( &xCertContext );
        pucCertDer = NULL;
    }

    if( pucCertDer != NULL )
    {
        mbedtls_free( pucCertDer );
    }

    mbedtls_entropy_free( &xEntropyCtx );

#ifdef MBEDTLS_TRANSPORT_PKCS11
    lError = lPKCS11PkMbedtlsCloseSessionAndFree( &xPkCtx );
#endif /* MBEDTLS_TRANSPORT_PKCS11 */
}


static void vSubCommand_GenerateKey( ConsoleIO_t * pxCIO,
                                     uint32_t ulArgc,
                                     char * ppcArgv[] )
{
    PkiStatus_t xStatus = PKI_SUCCESS;
    char * pcPrvKeyLabel = NULL;
    char * pcPubKeyLabel = NULL;
    unsigned char * pucPublicKeyDer = NULL;
    size_t uxPublicKeyDerLen = 0;

    pcPrvKeyLabel = TLS_KEY_PRV_LABEL;
    pcPubKeyLabel = TLS_KEY_PUB_LABEL;

    if( ( ulArgc > LABEL_PRV_IDX ) &&
        ( ppcArgv[ LABEL_PRV_IDX ] != NULL ) &&
        ( ppcArgv[ LABEL_PUB_IDX ] != NULL ) )
    {
        pcPrvKeyLabel = ppcArgv[ LABEL_PRV_IDX ];
        pcPubKeyLabel = ppcArgv[ LABEL_PUB_IDX ];
    }

    xStatus = xPkiGenerateECKeypair( pcPrvKeyLabel,
                                     pcPubKeyLabel,
                                     &pucPublicKeyDer,
                                     &uxPublicKeyDerLen );

    /* If successful, print public key in PEM form to terminal. */
    if( xStatus == PKI_SUCCESS )
    {
        pxCIO->print( "SUCCESS: Key pair generated and stored in\r\n" );
        pxCIO->print( "Private Key Label: " );
        pxCIO->write( pcPrvKeyLabel, strnlen( pcPrvKeyLabel, configTLS_MAX_LABEL_LEN ) );
        pxCIO->print( "\r\nPublic Key Label: " );
        pxCIO->write( pcPubKeyLabel, strnlen( pcPrvKeyLabel, configTLS_MAX_LABEL_LEN ) );
        pxCIO->print( "\r\n" );
    }
    else
    {
        pxCIO->print( "ERROR: Failed to generate public/private key pair.\r\n" );
    }

    /* Print DER public key as PEM */
    if( ( pucPublicKeyDer != NULL ) &&
        ( uxPublicKeyDerLen > 0 ) )
    {
        vPrintDer( pxCIO,
                   "-----BEGIN PUBLIC KEY-----\r\n",
                   "-----END PUBLIC KEY-----\r\n",
                   pucPublicKeyDer,
                   uxPublicKeyDerLen );
    }

    if( pucPublicKeyDer )
    {
        /* Free heap allocated memory */
        vPortFree( pucPublicKeyDer );
        pucPublicKeyDer = NULL;
    }
}


static BaseType_t xReadPemFromCliAsDer( ConsoleIO_t * pxCIO,
                                        unsigned char ** ppucDerBuffer,
                                        size_t * puxDerLen )
{
    size_t uxDerDataLen = 0;
    BaseType_t xBeginFound = pdFALSE;
    BaseType_t xEndFound = pdFALSE;
    BaseType_t xErrorFlag = pdFALSE;
    unsigned char * pucDerBuffer = NULL;

    configASSERT( pxCIO );
    configASSERT( ppucDerBuffer );
    configASSERT( puxDerLen );

    pucDerBuffer = mbedtls_calloc( 1, PEM_CERT_MAX_LEN );

    if( pucDerBuffer == NULL )
    {
        pxCIO->print( "Error: Out of memory to store the given PEM file.\r\n" );
        xErrorFlag = pdTRUE;
    }
    else
    {
        while( uxDerDataLen < PEM_CERT_MAX_LEN &&
               xEndFound == pdFALSE )
        {
            char * pcInputBuffer = NULL;
            int32_t lDataRead = pxCIO->readline( &pcInputBuffer );

            if( lDataRead > 0 )
            {
                size_t uxDataRead = ( size_t ) lDataRead;

                if( lDataRead > 64 )
                {
                    pxCIO->print( "Error: Current line exceeds maximum line length for a PEM file.\r\n" );
                    xErrorFlag = pdTRUE;
                }
                /* Check if this line will overflow the buffer given for the pem file */
                else if( ( xErrorFlag == pdFALSE ) &&
                         ( uxDataRead > 0 ) &&
                         ( ( uxDerDataLen + uxDataRead + PEM_LINE_ENDING_LEN ) >= PEM_CERT_MAX_LEN ) )
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
                                                     uxDataRead - strlen( PEM_BEGIN ) );

                        if( pcLabelEnd == NULL )
                        {
                            pxCIO->print( "Error: PEM header does not contain the expected ending: '" PEM_META_SUFFIX "'.\r\n" );
                            xErrorFlag = pdTRUE;
                        }
                        else
                        {
                            xBeginFound = pdTRUE;
                            continue;
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
                                                     uxDataRead - strlen( PEM_END ) );

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

                if( ( xBeginFound == pdTRUE ) &&
                    ( xEndFound == pdFALSE ) )
                {
                    size_t uxBytesWritten = 0;
                    int lRslt = mbedtls_base64_decode( &( pucDerBuffer[ uxDerDataLen ] ),
                                                       PEM_CERT_MAX_LEN - uxDerDataLen,
                                                       &uxBytesWritten,
                                                       ( unsigned char * ) pcInputBuffer, uxDataRead );

                    if( lRslt == 0 )
                    {
                        uxDerDataLen += uxBytesWritten;
                        configASSERT( uxDerDataLen < PEM_CERT_MAX_LEN );
                    }
                    else
                    {
                        xErrorFlag = pdTRUE;
                    }
                }
            }
            else
            {
                xErrorFlag = pdTRUE;
            }

            if( xErrorFlag == pdTRUE )
            {
                uxDerDataLen = 0;
                vPortFree( pucDerBuffer );
                pucDerBuffer = NULL;
                break;
            }
        }
    }

    if( pucDerBuffer != NULL )
    {
        *ppucDerBuffer = pucDerBuffer;
        *puxDerLen = uxDerDataLen;
    }
    else
    {
        xErrorFlag = pdTRUE;
    }

    return !xErrorFlag;
}

static BaseType_t xReadPemFromCliToX509Crt( ConsoleIO_t * pxCIO,
                                            mbedtls_x509_crt * pxCertificateContext )
{
    size_t uxDerDataLen = 0;
    BaseType_t xErrorFlag = pdFALSE;
    unsigned char * pucDerBuffer = NULL;

    configASSERT( pxCertificateContext );
    configASSERT( pxCIO );

    if( !xReadPemFromCliAsDer( pxCIO, &pucDerBuffer, &uxDerDataLen ) )
    {
        xErrorFlag = pdTRUE;
    }
    else
    {
        configASSERT( uxDerDataLen > 0 );
        mbedtls_x509_crt_init( pxCertificateContext );
        int lRslt = mbedtls_x509_crt_parse_der_nocopy( pxCertificateContext, pucDerBuffer, uxDerDataLen );

        if( lRslt < 0 )
        {
            vPortFree( pucDerBuffer );
            xErrorFlag = pdTRUE;
        }
    }

    return !xErrorFlag;
}

static void vSubCommand_ImportCertificate( ConsoleIO_t * pxCIO,
                                           uint32_t ulArgc,
                                           char * ppcArgv[] )
{
    BaseType_t xResult = pdTRUE;
    PkiStatus_t xStatus = PKI_SUCCESS;

    char pcCertLabel[ configTLS_MAX_LABEL_LEN + 1 ] = { 0 };
    size_t xCertLabelLen = 0;
    mbedtls_x509_crt xCertContext;

    mbedtls_x509_crt_init( &xCertContext );

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        ( void ) strncpy( pcCertLabel, ppcArgv[ LABEL_IDX ], configTLS_MAX_LABEL_LEN + 1 );
    }
    else
    {
        ( void ) strncpy( pcCertLabel, TLS_CERT_LABEL, configTLS_MAX_LABEL_LEN + 1 );
    }

    xCertLabelLen = strnlen( pcCertLabel, configTLS_MAX_LABEL_LEN );

    if( xCertLabelLen > configTLS_MAX_LABEL_LEN )
    {
        pxCIO->print( "Error: Certificate label: '" );
        pxCIO->print( pcCertLabel );
        pxCIO->print( "' is longer than the configured maximum length.\r\n" );
        xResult = pdFALSE;
    }

    if( xResult == pdTRUE )
    {
        xResult = xReadPemFromCliToX509Crt( pxCIO, &xCertContext );

        if( !xResult )
        {
            LogDebug( "xReadPemFromCliToX509Crt function failed." );
        }
    }

    if( xResult == pdTRUE )
    {
        xStatus = xPkiWriteCertificate( pcCertLabel, &xCertContext );

        if( xStatus == PKI_SUCCESS )
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
}

static void vSubCommand_ImportPubKey( ConsoleIO_t * pxCIO,
                                      uint32_t ulArgc,
                                      char * ppcArgv[] )
{
    BaseType_t xResult = pdTRUE;
    PkiStatus_t xStatus = PKI_SUCCESS;

    char pcPubKeyLabel[ configTLS_MAX_LABEL_LEN + 1 ] = { 0 };
    size_t xPubKeyLabelLen = 0;
    mbedtls_pk_context xPkContext;
    unsigned char * pucDerBuffer = NULL;
    size_t uxDerDataLen = 0;

    configASSERT( pxCIO );

    mbedtls_pk_init( &xPkContext );

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        ( void ) strncpy( pcPubKeyLabel, ppcArgv[ LABEL_IDX ], configTLS_MAX_LABEL_LEN + 1 );
    }
    else
    {
        ( void ) strncpy( pcPubKeyLabel, OTA_SIGNING_KEY_LABEL, configTLS_MAX_LABEL_LEN + 1 );
    }

    xPubKeyLabelLen = strnlen( pcPubKeyLabel, configTLS_MAX_LABEL_LEN );

    if( xPubKeyLabelLen > configTLS_MAX_LABEL_LEN )
    {
        pxCIO->print( "Error: Public Key label: '" );
        pxCIO->print( pcPubKeyLabel );
        pxCIO->print( "' is longer than the configured maximum length.\r\n" );
        xResult = pdFALSE;
    }

    if( xResult == pdTRUE )
    {
        xResult = xReadPemFromCliAsDer( pxCIO, &pucDerBuffer, &uxDerDataLen );
    }

    if( xResult == pdTRUE )
    {
        int lRslt = 0;

        lRslt = mbedtls_pk_parse_public_key( &xPkContext, pucDerBuffer, uxDerDataLen );

        if( lRslt != 0 )
        {
            pxCIO->print( "ERROR: Failed to parse public key." );
            xResult = pdFALSE;
        }
    }

    if( xResult == pdTRUE )
    {
        xStatus = xPkiWritePubKey( pcPubKeyLabel, pucDerBuffer, uxDerDataLen, &xPkContext );

        if( xStatus == PKI_SUCCESS )
        {
            pxCIO->print( "Success: Public Key loaded to label: '" );
            pxCIO->print( pcPubKeyLabel );
            pxCIO->print( "'.\r\n" );
        }
        else
        {
            pxCIO->print( "Error: failed to save public key to label: '" );
            pxCIO->print( pcPubKeyLabel );
            pxCIO->print( "'.\r\n" );
        }
    }

    if( pucDerBuffer != NULL )
    {
        mbedtls_free( pucDerBuffer );
        pucDerBuffer = NULL;
    }

    mbedtls_pk_free( &xPkContext );
}

static void vSubCommand_ExportCertificate( ConsoleIO_t * pxCIO,
                                           uint32_t ulArgc,
                                           char * ppcArgv[] )
{
    mbedtls_x509_crt xCertificateContext;
    char * pcCertLabel = NULL;
    PkiStatus_t xStatus = PKI_SUCCESS;
    PkiObject_t xCert;

    pcCertLabel = TLS_CERT_LABEL;
    mbedtls_x509_crt_init( &xCertificateContext );

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        pcCertLabel = ppcArgv[ LABEL_PUB_IDX ];
    }

    xCert = xPkiObjectFromLabel( pcCertLabel );

    xStatus = xPkiReadCertificate( &xCertificateContext, &xCert );

    if( xStatus == PKI_SUCCESS )
    {
        vPrintDer( pxCIO,
                   "-----BEGIN CERTIFICATE-----\r\n",
                   "-----END CERTIFICATE-----\r\n",
                   xCertificateContext.raw.p,
                   xCertificateContext.raw.len );

        mbedtls_x509_crt_free( &xCertificateContext );
    }
}

static void vSubCommand_ExportKey( ConsoleIO_t * pxCIO,
                                   uint32_t ulArgc,
                                   char * ppcArgv[] )
{
    PkiStatus_t xStatus = PKI_SUCCESS;
    char * pcPubKeyLabel = NULL;
    unsigned char * pucPublicKeyDer = NULL;
    size_t uxPublicKeyDerLen = 0;
    PkiObject_t xPubKeyObj;

    pcPubKeyLabel = TLS_KEY_PUB_LABEL;

    if( ( ulArgc > LABEL_IDX ) &&
        ( ppcArgv[ LABEL_IDX ] != NULL ) )
    {
        pcPubKeyLabel = ppcArgv[ LABEL_PUB_IDX ];
    }

    xPubKeyObj = xPkiObjectFromLabel( pcPubKeyLabel );

    xStatus = xPkiReadPublicKeyDer( &pucPublicKeyDer, &uxPublicKeyDerLen, &xPubKeyObj );

    /* If successful, print public key in PEM form to terminal. */
    if( xStatus == PKI_SUCCESS )
    {
        pxCIO->print( "Public Key Label: " );
    }
    else
    {
        pxCIO->print( "ERROR: Failed to locate public key with label: '" );
    }

    pxCIO->write( pcPubKeyLabel, strnlen( pcPubKeyLabel, configTLS_MAX_LABEL_LEN ) );
    pxCIO->print( "\r\n" );

    /* Print PEM public key */
    if( ( pucPublicKeyDer != NULL ) &&
        ( uxPublicKeyDerLen > 0 ) )
    {
        vPrintDer( pxCIO,
                   "-----BEGIN PUBLIC KEY-----\r\n",
                   "-----END PUBLIC KEY-----\r\n",
                   pucPublicKeyDer,
                   uxPublicKeyDerLen );

        /* Free heap allocated memory */
        vPortFree( pucPublicKeyDer );
        pucPublicKeyDer = NULL;
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
                    xSuccess = pdTRUE;
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
                    vSubCommand_ImportPubKey( pxCIO, ulArgc, ppcArgv );
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
