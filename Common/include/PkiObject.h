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

#ifndef _PKI_OBJECT_H_
#define _PKI_OBJECT_H_

#include "tls_transport_config.h"

#if defined( MBEDTLS_TRANSPORT_PSA )
#include "psa/crypto.h"
#include "psa/protected_storage.h"
#include "psa/internal_trusted_storage.h"
#endif /* MBEDTLS_TRANSPORT_PSA */

#if defined( MBEDTLS_TRANSPORT_PKCS11 )
#include "core_pkcs11.h"
#endif

#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/error.h"
#include "mbedtls/pem.h"

typedef enum PkiStatus
{
    PKI_SUCCESS = 0,
    PKI_ERR = -1,
    PKI_ERR_ARG_INVALID = -2,
    PKI_ERR_NOMEM = -3,
    PKI_ERR_INTERNAL = -6,
    PKI_ERR_NOT_IMPLEMENTED = -7,
    PKI_ERR_OBJ = -0x8000010,
    PKI_ERR_OBJ_NOT_FOUND = -0x8000011,
    PKI_ERR_OBJ_PARSING_FAILED = -0x8000012,
} PkiStatus_t;

typedef enum PkiObjectForm
{
    OBJ_FORM_NONE,
    OBJ_FORM_PEM,
    OBJ_FORM_DER,
#ifdef MBEDTLS_TRANSPORT_PKCS11
    OBJ_FORM_PKCS11_LABEL,
#endif
#ifdef MBEDTLS_TRANSPORT_PSA
    OBJ_FORM_PSA_CRYPTO,
    OBJ_FORM_PSA_ITS,
    OBJ_FORM_PSA_PS,
#endif
} PkiObjectForm_t;

typedef struct PkiObject
{
    PkiObjectForm_t xForm;
    size_t uxLen;
    union
    {
        const unsigned char * pucBuffer;
        const char * pcPkcs11Label;
#ifdef MBEDTLS_TRANSPORT_PSA
        psa_key_id_t xPsaCryptoId;
        psa_storage_uid_t xPsaStorageId;
#endif /* MBEDTLS_TRANSPORT_PSA */
    };
} PkiObject_t;

/* Convenience initializers */
#define PKI_OBJ_PEM( buffer, len )    { .xForm = OBJ_FORM_PEM, .uxLen = len, .pucBuffer = buffer }
#define PKI_OBJ_DER( buffer, len )    { .xForm = OBJ_FORM_DER, .uxLen = len, .pucBuffer = buffer }

#if defined( MBEDTLS_TRANSPORT_PKCS11 )
#define PKI_OBJ_PKCS11( label )       { .xForm = OBJ_FORM_PKCS11_LABEL, .uxLen = strlen( label ), .pcPkcs11Label = label }
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

#if defined( MBEDTLS_TRANSPORT_PSA )
#define PKI_OBJ_PSA_CRYPTO( key_id )     { .xForm = OBJ_FORM_PSA_CRYPTO, .xPsaCryptoId = key_id }
#define PKI_OBJ_PSA_ITS( storage_id )    { .xForm = OBJ_FORM_PSA_ITS, .xPsaStorageId = storage_id }
#define PKI_OBJ_PSA_PS( storage_id )     { .xForm = OBJ_FORM_PSA_PS, .xPsaStorageId = storage_id }
#endif /* MBEDTLS_TRANSPORT_PSA */

PkiObject_t xPkiObjectFromLabel( const char * pcLabel );

/**
 * @brief Add X509 certificate to a given mbedtls_x509_crt list.
 * @param[out] pxMbedtlsCertCtx Pointer to an mbedtls X509 certificate chain object.
 * @param[in] pxCertificate Pointer to a PkiObject_t describing a certificate to be loaded.
 * @param[in] pxTLSCtx Pointer to the TLS transport context.
 *
 * @return 0 on success; otherwise, failure;
 */
PkiStatus_t xPkiReadCertificate( mbedtls_x509_crt * pxMbedtlsCertCtx,
                                 const PkiObject_t * pxCertificate );

PkiStatus_t xPkiWriteCertificate( const char * pcCertLabel,
                                  const mbedtls_x509_crt * pxMbedtlsCertCtx );

/**
 * @brief Initialize the private key object
 *
 * @param[out] pxPkCtx Mbedtls pk_context object to map the key to.
 * @param[in] pxTLSCtx SSL context to which the private key is to be set.
 * @param[in] pxPrivateKey PkiObject_t representing the key to load.
 *
 * @return 0 on success; otherwise, failure;
 */
PkiStatus_t xPkiReadPrivateKey( mbedtls_pk_context * pxPkCtx,
                                const PkiObject_t * pxPrivateKey,
                                int ( * pxRngCallback )( void *, unsigned char *, size_t ),
                                void * pvRngCtx );

PkiStatus_t xPkiReadPublicKeyDer( unsigned char ** ppucPubKeyDer,
                                  size_t * uxPubKeyDerLen,
                                  const PkiObject_t * pxPublicKey );

PkiStatus_t xPkiReadPublicKey( mbedtls_pk_context * pxPkCtx,
                               const PkiObject_t * pxPublicKey );

PkiStatus_t xPkiWritePubKey( const char * pcPubKeyLabel,
                             const unsigned char * pucPubKeyDer,
                             const size_t uxPubKeyDerLen,
                             mbedtls_pk_context * pxPkContext );

PkiStatus_t xPkiGenerateECKeypair( const char * pcPrvKeyLabel,
                                   const char * pcPubKeyLabel,
                                   unsigned char ** ppucPubKeyDer,
                                   size_t * puxPubKeyDerLen );

#ifdef MBEDTLS_TRANSPORT_PKCS11
PkiStatus_t xPkcs11GenerateKeyPairEC( char * pcPrivateKeyLabel,
                                      char * pcPublicKeyLabel,
                                      unsigned char ** ppucPublicKeyDer,
                                      size_t * puxPublicKeyDerLen );

PkiStatus_t xPkcs11InitMbedtlsPkContext( const char * pcLabel,
                                         mbedtls_pk_context * pxPkCtx,
                                         CK_SESSION_HANDLE_PTR pxSessionHandle );

PkiStatus_t xPkcs11ReadCertificate( mbedtls_x509_crt * pxCertificateContext,
                                    const char * pcCertLabel );

PkiStatus_t xPkcs11WriteCertificate( const char * pcLabel,
                                     const mbedtls_x509_crt * pxCertificateContext );

BaseType_t xPkcs11ExportPublicKey( char * pcPubKeyLabel,
                                   unsigned char ** ppucPublicKeyDer,
                                   size_t * puxPubKeyDerLen );

PkiStatus_t xPkcs11ReadPublicKey( unsigned char ** ppucPublicKeyDer,
                                  size_t * puxPubKeyLen,
                                  const char * pcPubKeyLabel );

PkiStatus_t xPkcs11WritePubKey( const char * pcLabel,
                                const mbedtls_pk_context * pxPubKeyContext );

#endif /* MBEDTLS_TRANSPORT_PKCS11 */

#ifdef MBEDTLS_TRANSPORT_PSA

int32_t lPsa_initMbedtlsPkContext( mbedtls_pk_context * pxMbedtlsPkCtx,
                                   psa_key_id_t xKeyId );

int32_t lGenerateKeyPairECPsaCrypto( psa_key_id_t xPrvKeyId,
                                     psa_key_id_t xPubKeyId,
                                     unsigned char ** ppucPubKeyDer,
                                     size_t * puxPubKeyDerLen );
#endif /* MBEDTLS_TRANSPORT_PSA */

#endif /* _PKI_OBJECT_H_ */
