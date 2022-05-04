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

#include "tls_transport_config.h"

#ifdef MBEDTLS_TRANSPORT_PSA

#include "ota_config.h"

#include "FreeRTOS.h"

/* Standard Includes */
#include <string.h>
#include <stdlib.h>

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls_transport.h"

/* Mbedtls includes */
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform.h"
#include "mbedtls/error.h"
#include "mbedtls/asn1write.h"
#include "mbedtls/oid.h"


/* ARM PSA Includes */
#include "psa/crypto.h"
#include "psa/error.h"
#include "psa/protected_storage.h"
#include "psa/internal_trusted_storage.h"

#include "psa_util.h"

#define ECP_PUB_DER_MAX_BYTES    ( 30 + 2 * MBEDTLS_ECP_MAX_BYTES )
#define RSA_PUB_DER_MAX_BYTES    ( 38 + 2 * MBEDTLS_MPI_MAX_SIZE )


/*-----------------------------------------------------------*/

psa_status_t xReadPublicKeyFromPSACrypto( unsigned char ** ppucPubKeyDer,
                                          size_t * puxPubDerKeyLen,
                                          psa_key_id_t xKeyId )
{
    psa_status_t xStatus = PSA_SUCCESS;
    psa_key_attributes_t xKeyAttrs = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_pk_type_t xPkType = MBEDTLS_PK_NONE;
    size_t uxBytesWritten = 0;
    size_t uxObjLen = 0;

    unsigned char * pucAsn1WritePtr = NULL;

    if( ( ppucPubKeyDer == NULL ) ||
        ( puxPubDerKeyLen == NULL ) )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        *ppucPubKeyDer = NULL;
        *puxPubDerKeyLen = 0;

        xStatus = psa_get_key_attributes( xKeyId, &xKeyAttrs );
    }

    /* Allocate memory to store the public key in DER format */
    if( xStatus == PSA_SUCCESS )
    {
        if( PSA_KEY_TYPE_IS_ECC( xKeyAttrs.type ) )
        {
            *puxPubDerKeyLen = ECP_PUB_DER_MAX_BYTES;
            *ppucPubKeyDer = mbedtls_calloc( 1, *puxPubDerKeyLen );
        }
        else if( PSA_KEY_TYPE_IS_RSA( xKeyAttrs.type ) )
        {
            *puxPubDerKeyLen = RSA_PUB_DER_MAX_BYTES;
            *ppucPubKeyDer = mbedtls_calloc( 1, *puxPubDerKeyLen );
        }
        else
        {
            xStatus = PSA_ERROR_NOT_SUPPORTED;
        }

        if( *ppucPubKeyDer == NULL )
        {
            xStatus = PSA_ERROR_INSUFFICIENT_MEMORY;
        }
    }

    /* Construct a SubjectPublicKeyInfo object:
     *
     *  SubjectPublicKeyInfo  ::=  SEQUENCE
     *  {
     *       algorithm            AlgorithmIdentifier,
     *       subjectPublicKey     BIT STRING
     *  }
     */

    if( xStatus == PSA_SUCCESS )
    {
        /*
         * Check that the buffer is of sufficient length.
         * Set pucAsn1WritePtr to the last location in the buffer with sufficient space.
         */
        if( *puxPubDerKeyLen < PSA_EXPORT_PUBLIC_KEY_OUTPUT_SIZE( xKeyAttrs.type, xKeyAttrs.bits ) + 1 )
        {
            pucAsn1WritePtr = NULL;
            xStatus = PSA_ERROR_BUFFER_TOO_SMALL;
        }
        else
        {
            pucAsn1WritePtr = *ppucPubKeyDer;
        }

        /* Export the public key from PSA and write to buffer. */
        if( pucAsn1WritePtr != NULL )
        {
            if( PSA_KEY_TYPE_IS_PUBLIC_KEY( xKeyAttrs.type ) )
            {
                xStatus = psa_export_key( xKeyId, pucAsn1WritePtr, *puxPubDerKeyLen, &uxObjLen );
            }
            else if( PSA_KEY_TYPE_IS_KEY_PAIR( xKeyAttrs.type ) )
            {
                xStatus = psa_export_public_key( xKeyId, pucAsn1WritePtr, *puxPubDerKeyLen, &uxObjLen );
            }
            else
            {
                xStatus = PSA_ERROR_INVALID_HANDLE;
            }

            if( xStatus == PSA_SUCCESS )
            {
                pucAsn1WritePtr = &( pucAsn1WritePtr[ *puxPubDerKeyLen - uxObjLen ] );
                ( void ) memmove( pucAsn1WritePtr, *ppucPubKeyDer, uxObjLen );
                /* uxBytesWritten equal to the public key length written to the end of the buffer. */
            }

            /* HACK: Add a null byte */
            pucAsn1WritePtr--;
            *pucAsn1WritePtr = 0;
            uxObjLen++;
            uxBytesWritten = uxObjLen;
        }
    }

    /* Write length for public key object. */
    if( xStatus == PSA_SUCCESS )
    {
        int lRslt = 0;

        lRslt = mbedtls_asn1_write_len( &pucAsn1WritePtr, *ppucPubKeyDer, uxObjLen );

        if( lRslt > 0 )
        {
            uxBytesWritten += ( size_t ) lRslt;
        }
        else
        {
            xStatus = mbedtls_to_psa_error( lRslt );
        }
    }

    /* Write tag for public key object */
    if( xStatus == PSA_SUCCESS )
    {
        int lRslt = 0;

        lRslt = mbedtls_asn1_write_tag( &pucAsn1WritePtr, *ppucPubKeyDer, MBEDTLS_ASN1_BIT_STRING );

        if( lRslt > 0 )
        {
            uxBytesWritten += ( size_t ) lRslt;
        }
        else
        {
            xStatus = mbedtls_to_psa_error( lRslt );
        }
    }

    /*
     * Write AlgorithmIdentifier object
     *
     *  AlgorithmIdentifier ::= SEQUENCE {
     *      algorithm OBJECT IDENTIFIER,
     *      parameters ANY DEFINED BY algorithm OPTIONAL
     *  }
     */

    /* Handle parameters of AlgorithmIdentifier object */
    if( xStatus == PSA_SUCCESS )
    {
        int lRslt = 0;
        uxObjLen = 0;

        if( PSA_KEY_TYPE_IS_ECC( xKeyAttrs.type ) )
        {
            const char * pcNamedCurveOid = NULL;
            size_t uxNamedCurveOidLen = 0;

            /*
             *  Starting with ECParameters Named Curve
             *   ECParameters ::= CHOICE {
             *       namedCurve         OBJECT IDENTIFIER
             *   }
             */
            psa_ecc_family_t xFamily = PSA_KEY_TYPE_ECC_GET_FAMILY( xKeyAttrs.type );

            xPkType = MBEDTLS_PK_ECKEY;

            /* Determine the namedCurve OID */
            lRslt = mbedtls_psa_get_ecc_oid_from_id( xFamily, xKeyAttrs.bits,
                                                     &pcNamedCurveOid, &uxNamedCurveOidLen );

            /* Validate returned pointer and length */
            if( ( pcNamedCurveOid == NULL ) ||
                ( uxNamedCurveOidLen == 0 ) )
            {
                lRslt = MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
            }

            if( lRslt == 0 )
            {
                /* Write the namedCurve / ECParameters oid TLV */
                lRslt = mbedtls_asn1_write_oid( &pucAsn1WritePtr, *ppucPubKeyDer,
                                                pcNamedCurveOid, uxNamedCurveOidLen );

                if( lRslt > 0 )
                {
                    uxObjLen += ( size_t ) lRslt;
                }
            }
        }
        /* No parameters for RSA */
        else if( PSA_KEY_TYPE_IS_RSA( xKeyAttrs.type ) )
        {
            xPkType = MBEDTLS_PK_RSA;
        }
        else
        {
            lRslt = MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
        }

        xStatus = mbedtls_to_psa_error( lRslt );
    }

    /* Write algorithm OBJECT IDENTIFIER and close the AlgorithmIdentifier object */
    if( xStatus == PSA_SUCCESS )
    {
        const char * pcAlgorithmOid = NULL;
        size_t uxAlgorithmOidLen = 0;
        int lRslt = 0;

        /* Determine value for algorithm OBJECT IDENTIFIER */
        lRslt = mbedtls_oid_get_oid_by_pk_alg( xPkType,
                                               &pcAlgorithmOid, &uxAlgorithmOidLen );

        if( ( pcAlgorithmOid == NULL ) ||
            ( uxAlgorithmOidLen == 0 ) )
        {
            lRslt = MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
        }

        if( lRslt == 0 )
        {
            /* Write algorithm OBJECT IDENTIFIER and close AlgorithmIdentifier object */
            lRslt = mbedtls_asn1_write_algorithm_identifier( &pucAsn1WritePtr,
                                                             *ppucPubKeyDer,
                                                             pcAlgorithmOid, uxAlgorithmOidLen,
                                                             uxObjLen );

            if( lRslt > 0 )
            {
                uxBytesWritten += ( size_t ) lRslt;
            }
        }

        xStatus = mbedtls_to_psa_error( lRslt );
    }

    /* Close the SubjectPublicKeyInfo object */
    if( xStatus == PSA_SUCCESS )
    {
        int lRslt = 0;

        /* Write object len */
        lRslt = mbedtls_asn1_write_len( &pucAsn1WritePtr, *ppucPubKeyDer, uxBytesWritten );

        if( lRslt > 0 )
        {
            uxBytesWritten += ( size_t ) lRslt;
        }

        configASSERT( lRslt != 0 );

        if( lRslt > 0 )
        {
            lRslt = mbedtls_asn1_write_tag( &pucAsn1WritePtr, *ppucPubKeyDer,
                                            MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE );

            if( lRslt > 0 )
            {
                uxBytesWritten += ( size_t ) lRslt;
            }
        }

        xStatus = mbedtls_to_psa_error( lRslt );
    }

    /* Move DER object to the beginning of buffer */
    if( xStatus == PSA_SUCCESS )
    {
        const unsigned char * pucBufferEnd = &( ( *ppucPubKeyDer )[ *puxPubDerKeyLen ] );
        const unsigned char * pucDerStart = &( ( *ppucPubKeyDer )[ *puxPubDerKeyLen - uxBytesWritten ] );

        configASSERT( pucAsn1WritePtr >= *ppucPubKeyDer );
        configASSERT( pucAsn1WritePtr < pucBufferEnd );

        if( pucDerStart > *ppucPubKeyDer )
        {
            ( void ) memmove( *ppucPubKeyDer, pucDerStart, uxBytesWritten );
        }

        *puxPubDerKeyLen = uxBytesWritten;
    }

    if( xStatus != PSA_SUCCESS )
    {
        if( ( ppucPubKeyDer != NULL ) &&
            ( *ppucPubKeyDer != NULL ) )
        {
            mbedtls_free( *ppucPubKeyDer );
            *ppucPubKeyDer = NULL;
        }

        if( puxPubDerKeyLen != NULL )
        {
            *puxPubDerKeyLen = 0;
        }
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

int32_t lWritePublicKeyToPSACrypto( psa_key_id_t xPubKeyId,
                                    const mbedtls_pk_context * pxPublicKeyContext )
{
    psa_status_t xStatus = PSA_SUCCESS;
    psa_key_attributes_t xKeyAttributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_pk_type_t xKeyType = MBEDTLS_PK_NONE;


    unsigned char * pucPubKeyBuffer = NULL;
    size_t uxPubKeyBufferLen = 0;

    configASSERT( xPubKeyId );
    configASSERT( pxPublicKeyContext );

    xKeyType = mbedtls_pk_get_type( pxPublicKeyContext );

    if( xKeyType == MBEDTLS_PK_ECKEY )
    {
        mbedtls_ecdsa_context * pxEcpKey = ( mbedtls_ecdsa_context * ) pxPublicKeyContext->pk_ctx;

        psa_ecc_family_t xKeyFamily = xPsaFamilyFromMbedtlsEccGroupId( pxEcpKey->grp.id );

        psa_set_key_type( &xKeyAttributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY( xKeyFamily ) );

        psa_set_key_algorithm( &xKeyAttributes, PSA_ALG_ECDSA( PSA_ALG_ANY_HASH ) );

        psa_set_key_id( &xKeyAttributes, xPubKeyId );

        psa_set_key_lifetime( &xKeyAttributes, PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(
                                  PSA_KEY_LIFETIME_PERSISTENT, PSA_KEY_LOCATION_LOCAL_STORAGE ) );

        psa_set_key_bits( &xKeyAttributes, mbedtls_pk_get_bitlen( pxPublicKeyContext ) );

        psa_set_key_usage_flags( &xKeyAttributes, PSA_KEY_USAGE_VERIFY_MESSAGE |
                                 PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_EXPORT );

        uxPubKeyBufferLen = PSA_KEY_EXPORT_ECC_PUBLIC_KEY_MAX_SIZE( xKeyAttributes.bits );

        pucPubKeyBuffer = mbedtls_calloc( 1, uxPubKeyBufferLen );

        if( pucPubKeyBuffer == NULL )
        {
            xStatus = PSA_ERROR_INSUFFICIENT_MEMORY;
        }
        else
        {
            /* Write EC point to buffer */
            int lRslt = mbedtls_ecp_point_write_binary( &( pxEcpKey->grp ),
                                                        &( pxEcpKey->Q ),
                                                        MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                        &uxPubKeyBufferLen,
                                                        pucPubKeyBuffer,
                                                        uxPubKeyBufferLen );

            if( lRslt != 0 )
            {
                xStatus = PSA_ERROR_GENERIC_ERROR;
            }
        }
    }
    else
    {
        xStatus = PSA_ERROR_NOT_SUPPORTED;
    }

    /* Export key to PSA */
    if( xStatus == PSA_SUCCESS )
    {
        xStatus = psa_import_key( &xKeyAttributes,
                                  pucPubKeyBuffer,
                                  uxPubKeyBufferLen,
                                  &( xKeyAttributes.id ) );
    }

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lWriteCertificateToPSACrypto( psa_key_id_t xCertId,
                                      const mbedtls_x509_crt * pxCertificateContext )
{
    psa_status_t xStatus = PSA_SUCCESS;
    psa_key_attributes_t xCertAttrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t xKeyId;

    configASSERT( xCertId >= PSA_KEY_ID_USER_MIN );
    configASSERT( xCertId <= PSA_KEY_ID_VENDOR_MAX );

    if( pxCertificateContext == NULL )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        psa_key_lifetime_t xKeyLifetime = PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(
            PSA_KEY_LIFETIME_PERSISTENT, PSA_KEY_LOCATION_LOCAL_STORAGE );

        psa_set_key_id( &xCertAttrs, xCertId );

        psa_set_key_lifetime( &xCertAttrs, xKeyLifetime );

        psa_set_key_type( &xCertAttrs, PSA_KEY_TYPE_RAW_DATA );

        psa_set_key_bits( &xCertAttrs, 8 * pxCertificateContext->raw.len );

        psa_set_key_usage_flags( &xCertAttrs, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_COPY );

        psa_set_key_algorithm( &xCertAttrs, 0 );

        xStatus = psa_import_key( &xCertAttrs,
                                  pxCertificateContext->raw.p,
                                  pxCertificateContext->raw.len,
                                  &xKeyId );

        ( void ) xKeyId;
    }

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lReadCertificateFromPSACrypto( mbedtls_x509_crt * pxCertificateContext,
                                       psa_key_id_t xCertId )
{
    psa_status_t xStatus = PSA_SUCCESS;
    unsigned char * pucCertBuffer = NULL;
    size_t uxCertLen = 0;

    configASSERT( xCertId >= PSA_KEY_ID_USER_MIN );
    configASSERT( xCertId <= PSA_KEY_ID_VENDOR_MAX );

    if( pxCertificateContext == NULL )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        xStatus = xReadObjectFromPSACrypto( &pucCertBuffer, &uxCertLen, xCertId );
    }

    if( xStatus == PSA_SUCCESS )
    {
        xStatus = mbedtls_x509_crt_parse_der_nocopy( pxCertificateContext,
                                                     pucCertBuffer,
                                                     uxCertLen );
    }

    /* Free memory on error. */
    if( ( xStatus != PSA_SUCCESS ) &&
        ( pucCertBuffer != NULL ) )
    {
        mbedtls_free( pucCertBuffer );
        pucCertBuffer = NULL;
    }

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lWriteCertificateToPsaIts( psa_storage_uid_t xCertUid,
                                   const mbedtls_x509_crt * pxCertificateContext )
{
    psa_status_t xStatus;

    if( ( pxCertificateContext == NULL ) ||
        ( pxCertificateContext->raw.p == NULL ) ||
        ( pxCertificateContext->raw.len == 0 ) )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        xStatus = psa_its_set( xCertUid,
                               pxCertificateContext->raw.len,
                               pxCertificateContext->raw.p,
                               PSA_STORAGE_FLAG_NONE );
    }

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lReadCertificateFromPsaIts( mbedtls_x509_crt * pxCertificateContext,
                                    psa_storage_uid_t xCertUid )
{
    int32_t lError = 0;
    uint8_t * pucCertBuffer = NULL;
    size_t uxCertLen = 0;

    configASSERT( xCertUid > 0 );

    if( pxCertificateContext == NULL )
    {
        lError = MBEDTLS_ERR_ERROR_GENERIC_ERROR;
    }

    if( lError == 0 )
    {
        lError = lReadObjectFromPsaIts( &pucCertBuffer, &uxCertLen, xCertUid );
    }

    if( lError == 0 )
    {
        /* Determine if certificate is in pem or der format */
        if( ( uxCertLen != 0 ) && ( pucCertBuffer[ uxCertLen - 1 ] == '\0' ) &&
            ( strstr( ( const char * ) pucCertBuffer, "-----BEGIN CERTIFICATE-----" ) != NULL ) )
        {
            lError = mbedtls_x509_crt_parse( pxCertificateContext,
                                             pucCertBuffer,
                                             uxCertLen );
        }
        /* Handle DER format */
        else
        {
            lError = mbedtls_x509_crt_parse_der_nocopy( pxCertificateContext,
                                                        pucCertBuffer,
                                                        uxCertLen );

            /*
             * pxCertificateContext takes ownership of allocated buffer in this case.
             * Clear the pointer to prevent it from being freed.
             */
            if( lError == 0 )
            {
                pucCertBuffer = NULL;
            }
        }
    }

    /* Free memory. */
    if( pucCertBuffer != NULL )
    {
        mbedtls_free( pucCertBuffer );
    }

    return lError;
}

/*-----------------------------------------------------------*/

int32_t lWriteCertificateToPsaPS( psa_storage_uid_t xCertUid,
                                  const mbedtls_x509_crt * pxCertificateContext )
{
    psa_status_t xStatus;

    if( ( pxCertificateContext == NULL ) ||
        ( pxCertificateContext->raw.p == NULL ) ||
        ( pxCertificateContext->raw.len == 0 ) )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        xStatus = psa_ps_set( xCertUid,
                              pxCertificateContext->raw.len,
                              pxCertificateContext->raw.p,
                              PSA_STORAGE_FLAG_NONE );
    }

    return mbedtls_psa_err_translate_pk( xStatus );
}

/*-----------------------------------------------------------*/

int32_t lReadCertificateFromPsaPS( mbedtls_x509_crt * pxCertificateContext,
                                   psa_storage_uid_t xCertUid )
{
    int32_t lError = 0;
    uint8_t * pucCertBuffer = NULL;
    size_t uxCertLen = 0;

    configASSERT( xCertUid > 0 );

    if( pxCertificateContext == NULL )
    {
        lError = MBEDTLS_ERR_ERROR_GENERIC_ERROR;
    }

    if( lError == 0 )
    {
        lError = lReadObjectFromPsaPs( &pucCertBuffer, &uxCertLen, xCertUid );
    }

    if( lError == 0 )
    {
        /* Determine if certificate is in pem or der format */
        if( ( uxCertLen != 0 ) && ( pucCertBuffer[ uxCertLen - 1 ] == '\0' ) &&
            ( strstr( ( const char * ) pucCertBuffer, "-----BEGIN CERTIFICATE-----" ) != NULL ) )
        {
            lError = mbedtls_x509_crt_parse( pxCertificateContext,
                                             pucCertBuffer,
                                             uxCertLen );
        }
        /* Handle DER format */
        else
        {
            lError = mbedtls_x509_crt_parse_der_nocopy( pxCertificateContext,
                                                        pucCertBuffer,
                                                        uxCertLen );

            /*
             * pxCertificateContext takes ownership of allocated buffer in this case.
             * Clear the pointer to prevent it from being freed.
             */
            if( lError == 0 )
            {
                pucCertBuffer = NULL;
            }
        }
    }

    /* Free memory. */
    if( pucCertBuffer != NULL )
    {
        mbedtls_free( pucCertBuffer );
    }

    return lError;
}

/*-----------------------------------------------------------*/


int32_t lGenerateKeyPairECPsaCrypto( psa_key_id_t xPrvKeyId,
                                     psa_key_id_t xPubKeyId,
                                     unsigned char ** ppucPubKeyDer,
                                     size_t * puxPubKeyDerLen )
{
    psa_status_t xStatus = PSA_SUCCESS;
    psa_key_attributes_t xKeyAttrs = PSA_KEY_ATTRIBUTES_INIT;

    if( ( xPrvKeyId == 0 ) ||
        ( xPubKeyId == 0 ) )
    {
        xStatus = PSA_ERROR_INVALID_HANDLE;
    }
    else if( ( ppucPubKeyDer == NULL ) ||
             ( puxPubKeyDerLen == NULL ) )
    {
        xStatus = PSA_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        *ppucPubKeyDer = NULL;
        *puxPubKeyDerLen = 0;

        xStatus = psa_destroy_key( xPrvKeyId );

        /* Mask invalid handle error since key may not exist */
        if( xStatus == PSA_ERROR_INVALID_HANDLE )
        {
            xStatus = PSA_SUCCESS;
        }

        if( xStatus == PSA_SUCCESS )
        {
            xStatus = psa_destroy_key( xPubKeyId );
        }

        /* Mask invalid handle error since key may not exist */
        if( xStatus == PSA_ERROR_INVALID_HANDLE )
        {
            xStatus = PSA_SUCCESS;
        }
    }

    if( xStatus == PSA_SUCCESS )
    {
        psa_set_key_lifetime( &xKeyAttrs,
                              PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(
                                  PSA_KEY_LIFETIME_PERSISTENT,
                                  PSA_KEY_LOCATION_LOCAL_STORAGE ) );

        psa_set_key_id( &xKeyAttrs, xPrvKeyId );

        psa_set_key_type( &xKeyAttrs, PSA_KEY_TYPE_ECC_KEY_PAIR( PSA_ECC_FAMILY_SECP_R1 ) );

        psa_set_key_bits( &xKeyAttrs, 256 );

        psa_set_key_usage_flags( &xKeyAttrs, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH |
                                 PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE |
                                 PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_VERIFY_DERIVATION |
                                 PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT );

        psa_set_key_algorithm( &xKeyAttrs, PSA_ALG_ECDSA( PSA_ALG_SHA_256 ) );
        xStatus = psa_generate_key( &xKeyAttrs, &xPrvKeyId );
    }

    if( xStatus == PSA_SUCCESS )
    {
        xStatus = xReadPublicKeyFromPSACrypto( ppucPubKeyDer,
                                               puxPubKeyDerLen,
                                               xPrvKeyId );
    }

    if( xStatus == PSA_SUCCESS )
    {
        size_t uxPubKeyLen = PSA_EXPORT_PUBLIC_KEY_OUTPUT_SIZE( xKeyAttrs.type, xKeyAttrs.bits );
        const unsigned char * pucPubKey = &( ( *ppucPubKeyDer )[ *puxPubKeyDerLen - uxPubKeyLen ] );

        psa_set_key_type( &xKeyAttrs, PSA_KEY_TYPE_ECC_PUBLIC_KEY( PSA_ECC_FAMILY_SECP_R1 ) );
        psa_set_key_id( &xKeyAttrs, xPubKeyId );

        psa_set_key_usage_flags( &xKeyAttrs, PSA_KEY_USAGE_EXPORT );

        xStatus = psa_import_key( &xKeyAttrs, pucPubKey, uxPubKeyLen, &xPubKeyId );
    }

    if( xStatus != PSA_SUCCESS )
    {
        if( *ppucPubKeyDer != NULL )
        {
            mbedtls_free( *ppucPubKeyDer );
            *ppucPubKeyDer = NULL;
        }

        *puxPubKeyDerLen = 0;
    }

    return mbedtls_psa_err_translate_pk( xStatus );
}

#endif /* MBEDTLS_TRANSPORT_PSA */
