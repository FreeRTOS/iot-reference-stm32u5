/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 * @file mbedtls_transport.h
 * @brief TLS transport interface header.
 */

#ifndef _MBEDTLS_TRANSPORT_H
#define _MBEDTLS_TRANSPORT_H

#include "mbedtls_error_utils.h"
#include "transport_interface.h"

/* socket definitions  */
#include "lwip/netdb.h"

/* mbed TLS includes. */
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ssl.h"
#include "mbedtls/threading.h"
#include "mbedtls/x509.h"
#include "pk_wrap.h"

#include "tls_transport_config.h"

#include "PkiObject.h"

#ifdef MBEDTLS_TRANSPORT_PKCS11
#include "core_pkcs11_config.h"
#include "core_pkcs11.h"
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

#ifdef MBEDTLS_TRANSPORT_PSA
#include "psa/crypto.h"
#include "psa/internal_trusted_storage.h"
#include "psa/protected_storage.h"
#endif /* MBEDTLS_TRANSPORT_PSA */

/*
 * Error codes
 */
#ifndef SOCK_OK
#define SOCK_OK    0
#endif


/* Public Types */
typedef enum
{
    STATE_UNKNOWN = 0,
    STATE_ALLOCATED = 1,
    STATE_CONFIGURED = 2,
    STATE_CONNECTED = 3,
} ConnectionState_t;

typedef enum TlsTransportStatus
{
    TLS_TRANSPORT_SUCCESS = PKI_SUCCESS,
    TLS_TRANSPORT_UNKNOWN_ERROR = PKI_ERR,
    TLS_TRANSPORT_INVALID_PARAMETER = PKI_ERR_ARG_INVALID,
    TLS_TRANSPORT_INSUFFICIENT_MEMORY = PKI_ERR_NOMEM,
    TLS_TRANSPORT_INVALID_CREDENTIALS = -4,
    TLS_TRANSPORT_HANDSHAKE_FAILED = -5,
    TLS_TRANSPORT_INTERNAL_ERROR = PKI_ERR_INTERNAL,
    TLS_TRANSPORT_CONNECT_FAILURE = -7,
    TLS_TRANSPORT_PKI_OBJECT_NOT_FOUND = PKI_ERR_OBJ_NOT_FOUND,
    TLS_TRANSPORT_PKI_OBJECT_PARSE_FAIL = PKI_ERR_OBJ_PARSING_FAILED,
    TLS_TRANSPORT_DNS_FAILED = -10,
    TLS_TRANSPORT_INSUFFICIENT_SOCKETS = -11,
    TLS_TRANSPORT_INVALID_HOSTNAME = -12,
    TLS_TRANSPORT_CLIENT_CERT_INVALID = -13,
    TLS_TRANSPORT_NO_VALID_CA_CERT = -14,
    TLS_TRANSPORT_CLIENT_KEY_INVALID = -15,
} TlsTransportStatus_t;

typedef void ( * GenericCallback_t )( void * );

/*-----------------------------------------------------------*/

/**
 * @brief Allocate a TLS Network Context
 *
 * @return pointer to a NetworkContext_t used by the TLS stack.
 */
NetworkContext_t * mbedtls_transport_allocate( void );

/**
 * @brief Deallocate a TLS NetworkContext_t.
 */
void mbedtls_transport_free( NetworkContext_t * pxNetworkContext );



TlsTransportStatus_t mbedtls_transport_configure( NetworkContext_t * pxNetworkContext,
                                                  const char ** ppcAlpnProtos,
                                                  const PkiObject_t * pxPrivateKey,
                                                  const PkiObject_t * pxClientCert,
                                                  const PkiObject_t * pxRootCaCerts,
                                                  const size_t uxNumRootCA );


int32_t mbedtls_transport_setrecvcallback( NetworkContext_t * pxNetworkContext,
                                           GenericCallback_t pxCallback,
                                           void * pvCtx );


/**
 * @brief Create a TLS connection
 *
 * @param[out] pNetworkContext Pointer to a network context to contain the
 * initialized socket handle.
 * @param[in] pHostName The hostname of the remote endpoint.
 * @param[in] port The destination port.
 * @param[in] receiveTimeoutMs Receive socket timeout.
 * @param[in] sendTimeoutMs Send socket timeout.
 *
 * @return #TLS_TRANSPORT_SUCCESS, #TLS_TRANSPORT_INSUFFICIENT_MEMORY, #TLS_TRANSPORT_INVALID_CREDENTIALS,
 * #TLS_TRANSPORT_HANDSHAKE_FAILED, #TLS_TRANSPORT_INTERNAL_ERROR, or #TLS_TRANSPORT_CONNECT_FAILURE.
 */
TlsTransportStatus_t mbedtls_transport_connect( NetworkContext_t * pxNetworkContext,
                                                const char * pcHostName,
                                                uint16_t usPort,
                                                uint32_t ulRecvTimeoutMs,
                                                uint32_t ulSendTimeoutMs );

/**
 * @brief Sets the socket option for the underlying socket connection.
 *
 * @param[out] pNetworkContext Pointer to a network context that contains the
 * initialized socket handle.
 * @param[in] lSockopt Socket option to be set
 * @param[in] pvSockoptValue Pointer to memory area containing the value of the socket option.
 * @param[in] ulOptionLen Length of the memory area containing the value of the socket option.
 *
 * @return 0 on success, negative error code on failure.
 */
int32_t mbedtls_transport_setsockopt( NetworkContext_t * pxNetworkContext,
                                      int32_t lSockopt,
                                      const void * pvSockoptValue,
                                      uint32_t ulOptionLen );

/**
 * @brief Gracefully disconnect an established TLS connection.
 *
 * @param[in] pNetworkContext Network context.
 */
void mbedtls_transport_disconnect( NetworkContext_t * pxNetworkContext );

/**
 * @brief Receives data from an established TLS connection.
 *
 * This is the TLS version of the transport interface's
 * #TransportRecv_t function.
 *
 * @return Number of bytes (> 0) received if successful;
 * 0 if the socket times out without reading any bytes;
 * negative value on error.
 */
int32_t mbedtls_transport_recv( NetworkContext_t * pxNetworkContext,
                                void * pBuffer,
                                size_t bytesToRecv );

/**
 * @brief Sends data over an established TLS connection.
 *
 * This is the TLS version of the transport interface's
 * #TransportSend_t function.
 *
 * @return Number of bytes (> 0) sent on success;
 * 0 if the socket times out without sending any bytes;
 * else a negative value to represent error.
 */
int32_t mbedtls_transport_send( NetworkContext_t * pxNetworkContext,
                                const void * pBuffer,
                                size_t uxBytesToSend );


#ifdef MBEDTLS_TRANSPORT_PKCS11
extern mbedtls_pk_info_t mbedtls_pkcs11_pk_ecdsa;
extern mbedtls_pk_info_t mbedtls_pkcs11_pk_rsa;

int32_t lReadCertificateFromPKCS11( mbedtls_x509_crt * pxCertificateContext,
                                    CK_SESSION_HANDLE xP11SessionHandle,
                                    const char * pcCertificateLabel,
                                    size_t xLabelLen );

int32_t lWriteCertificateToPKCS11( const mbedtls_x509_crt * pxCertificateContext,
                                   CK_SESSION_HANDLE xP11SessionHandle,
                                   char * pcCertificateLabel,
                                   size_t uxCertificateLabelLen );

int32_t lWriteEcPublicKeyToPKCS11( const mbedtls_pk_context * pxPubKeyContext,
                                   CK_SESSION_HANDLE xP11SessionHandle,
                                   char * pcPubKeyLabel,
                                   size_t uxPubKeyLabelLen );

CK_RV xPKCS11_initMbedtlsPkContext( mbedtls_pk_context * pxMbedtlsPkCtx,
                                    CK_SESSION_HANDLE xSessionHandle,
                                    CK_OBJECT_HANDLE xPkHandle );

const char * pcPKCS11StrError( CK_RV xError );

int lPKCS11RandomCallback( void * pvCtx,
                           unsigned char * pucOutput,
                           size_t uxLen );

int lPKCS11PkMbedtlsCloseSessionAndFree( mbedtls_pk_context * pxMbedtlsPkCtx );

#endif /* MBEDTLS_TRANSPORT_PKCS11 */

#ifdef MBEDTLS_TRANSPORT_PSA

int32_t lWriteCertificateToPSACrypto( psa_key_id_t xCertId,
                                      const mbedtls_x509_crt * pxCertificateContext );

psa_status_t xReadObjectFromPSACrypto( unsigned char ** ppucObject,
                                       size_t * puxObjectLen,
                                       psa_key_id_t xCertId );

psa_status_t xReadPublicKeyFromPSACrypto( unsigned char ** ppucPubKeyDer,
                                          size_t * puxPubDerKeyLen,
                                          psa_key_id_t xKeyId );

int32_t lWritePublicKeyToPSACrypto( psa_key_id_t xPubKeyId,
                                    const mbedtls_pk_context * pxPublicKeyContext );

int32_t lReadCertificateFromPSACrypto( mbedtls_x509_crt * pxCertificateContext,
                                       psa_key_id_t xCertId );

int32_t lWriteObjectToPsaIts( psa_storage_uid_t xObjectUid,
                              const uint8_t * pucData,
                              size_t uxDataLen );

int32_t lReadObjectFromPsaIts( uint8_t ** ppucData,
                               size_t * puxDataLen,
                               psa_storage_uid_t xObjectUid );

int32_t lWriteObjectToPsaPs( psa_storage_uid_t xObjectUid,
                             const uint8_t * pucData,
                             size_t uxDataLen );

int32_t lReadObjectFromPsaPs( uint8_t ** ppucData,
                              size_t * puxDataLen,
                              psa_storage_uid_t xObjectUid );

int32_t lWriteCertificateToPsaIts( psa_storage_uid_t xCertUid,
                                   const mbedtls_x509_crt * pxCertificateContext );

int32_t lReadCertificateFromPsaIts( mbedtls_x509_crt * pxCertificateContext,
                                    psa_storage_uid_t xCertUid );

int32_t lWriteCertificateToPsaPS( psa_storage_uid_t xCertUid,
                                  const mbedtls_x509_crt * pxCertificateContext );

int32_t lReadCertificateFromPsaPS( mbedtls_x509_crt * pxCertificateContext,
                                   psa_storage_uid_t xCertUid );

int lPSARandomCallback( void * pvCtx,
                        unsigned char * pucOutput,
                        size_t uxLen );

int32_t lPsa_initMbedtlsPkContext( mbedtls_pk_context * pxMbedtlsPkCtx,
                                   psa_key_id_t xKeyId );

#endif /* MBEDTLS_TRANSPORT_PSA */

#endif /* _MBEDTLS_TRANSPORT_H */
