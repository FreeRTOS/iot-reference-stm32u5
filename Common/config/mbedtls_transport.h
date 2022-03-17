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

#ifndef MBEDTLS_TRANSPORT
#define MBEDTLS_TRANSPORT

/* socket definitions  */
#include "transport_interface_ext.h"

#include "lwip/netdb.h"

/* mbed TLS includes. */
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ssl.h"
#include "mbedtls/threading.h"
#include "mbedtls/x509.h"
#include "pk_wrap.h"
#include "tls_transport_config.h"

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
 * Define MBEDTLS_DEBUG_C if you would like to enable mbedtls debugging
 */
/* #define MBEDTLS_DEBUG_C */

typedef enum
{
	STATE_UNKNOWN = 0,
	STATE_ALLOCATED = 1,
	STATE_CONFIGURED = 2,
	STATE_CONNECTED = 3,
} ConnectionState_t;

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
		char * pcPkcs11Label;
#ifdef MBEDTLS_TRANSPORT_PSA
		psa_key_id_t xPsaCryptoId;
		psa_storage_uid_t xPsaStorageId;
#endif /* MBEDTLS_TRANSPORT_PSA */
	};
} PkiObject_t;

/* Convenience initializers */
#define PKI_OBJ_PEM( buffer, len ) 		{ .xForm = OBJ_FORM_PEM, 			.uxLen = len, 					.pucBuffer = buffer }
#define PKI_OBJ_DER( buffer, len ) 		{ .xForm = OBJ_FORM_DER, 			.uxLen = len, 					.pucBuffer = buffer }
#define PKI_OBJ_PKCS11( label ) 		{ .xForm = OBJ_FORM_PKCS11_LABEL, 	.uxLen = strlen( label ), 		.pcPkcs11Label = label }
#define PKI_OBJ_PSA_CRYPTO( key_id ) 	{ .xForm = OBJ_FORM_PSA_CRYPTO, 	.xPsaCryptoId = key_id }
#define PKI_OBJ_PSA_ITS( storage_id ) 	{ .xForm = OBJ_FORM_PSA_ITS, 		.xPsaStorageId = storage_id }
#define PKI_OBJ_PSA_PS( storage_id ) 	{ .xForm = OBJ_FORM_PSA_PS, 		.xPsaStorageId = storage_id }

#define sock_socket 	lwip_socket
#define sock_connect 	lwip_connect
#define sock_send 		lwip_send
#define sock_recv 		lwip_recv
#define sock_close 		lwip_close
#define sock_setsockopt lwip_setsockopt
#define sock_fcntl		lwip_fcntl

#define dns_getaddrinfo lwip_getaddrinfo
#define dns_freeaddrinfo lwip_freeaddrinfo

typedef int SockHandle_t;

//typedef union PkiObject
//{
//	struct
//	{
//		size_t uxLen;
//		const unsigned char * pucBuffer;
//	} xBuffer;
//
//	char * pcPkcs11Label;
//	psa_key_id_t xPsaCryptoId;
//	psa_storage_uid_t xPsaStorageId;
//} PkiObject_t;

/**
 * @brief TLS Connect / Disconnect return status.
 */
typedef enum TlsTransportStatus
{
    TLS_TRANSPORT_SUCCESS 				= 0,
	TLS_TRANSPORT_UNKNOWN_ERROR			= -1,
    TLS_TRANSPORT_INVALID_PARAMETER 	= -2,
    TLS_TRANSPORT_INSUFFICIENT_MEMORY 	= -3,
    TLS_TRANSPORT_INVALID_CREDENTIALS 	= -4,
    TLS_TRANSPORT_HANDSHAKE_FAILED 		= -5,
    TLS_TRANSPORT_INTERNAL_ERROR 		= -6,
    TLS_TRANSPORT_CONNECT_FAILURE  		= -7,
	TLS_TRANSPORT_PKI_OBJECT_NOT_FOUND 	= -8,
	TLS_TRANSPORT_PKI_OBJECT_PARSE_FAIL = -9,
	TLS_TRANSPORT_DNS_FAILED			= -10,
	TLS_TRANSPORT_INSUFFICIENT_SOCKETS  = -11,
	TLS_TRANSPORT_INVALID_HOSTNAME      = -12,
	TLS_TRANSPORT_CLIENT_CERT_INVALID   = -13,
	TLS_TRANSPORT_NO_VALID_CA_CERT      = -14,
	TLS_TRANSPORT_CLIENT_KEY_INVALID    = -15,
} TlsTransportStatus_t;


/**
 * @brief Allocate a TLS Network Context
 *
 * @return pointer to a NetworkContext_t used by the TLS stack.
 */
NetworkContext_t * mbedtls_transport_allocate( void );


/**
 * @brief Deallocate a TLS Network Context
 *
 * @param[in] pxNetworkContext The network context to be deallocated.
 *
 */
void mbedtls_transport_free( NetworkContext_t * pxNetworkContext );



TlsTransportStatus_t mbedtls_transport_configure( NetworkContext_t * pxNetworkContext,
												  const char ** ppcAlpnProtos,
												  const PkiObject_t * pxPrivateKey,
												  const PkiObject_t * pxClientCert,
												  const PkiObject_t * pxRootCaCerts,
												  const size_t uxNumRootCA );


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
 * @brief Gracefully disconnect an established TLS connection and free any heap allocated resources.
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
 * @param[in] pNetworkContext The Network context.
 * @param[out] pBuffer Buffer to receive bytes into.
 * @param[in] bytesToRecv Number of bytes to receive from the network.
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
 * @param[in] pNetworkContext The network context.
 * @param[in] pBuffer Buffer containing the bytes to send.
 * @param[in] uxBytesToSend Number of bytes to send from the buffer.
 *
 * @return Number of bytes (> 0) sent on success;
 * 0 if the socket times out without sending any bytes;
 * else a negative value to represent error.
 */
int32_t mbedtls_transport_send( NetworkContext_t * pxNetworkContext,
                                const void * pBuffer,
                                size_t uxBytesToSend );


#ifdef MBEDTLS_TRANSPORT_PKCS11
mbedtls_pk_info_t mbedtls_pkcs11_pk_ecdsa;
mbedtls_pk_info_t mbedtls_pkcs11_pk_rsa;

int32_t lReadCertificateFromPKCS11( mbedtls_x509_crt * pxCertificateContext,
									CK_SESSION_HANDLE xP11SessionHandle,
                                    const char * pcCertificateLabel,
									size_t xLabelLen );

int32_t lPKCS11_initMbedtlsPkContext( mbedtls_pk_context * pxMbedtlsPkCtx,
									  CK_SESSION_HANDLE xSessionHandle,
									  CK_OBJECT_HANDLE xPkHandle );

const char * pcPKCS11StrError( CK_RV xError );

int lPKCS11RandomCallback( void * pvCtx, unsigned char * pucOutput,
						   size_t uxLen );
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

#ifdef MBEDTLS_TRANSPORT_PSA

int32_t lReadCertificateFromPSACrypto( mbedtls_x509_crt * pxCertificateContext,
									   psa_key_id_t xCertId );

int32_t lLoadObjectFromPsaPs( uint8_t ** ppucData,
							  size_t * puxDataLen,
							  psa_storage_uid_t xObjectUid );

int32_t lLoadObjectFromPsaIts( uint8_t ** ppucData,
							   size_t * puxDataLen,
							   psa_storage_uid_t xObjectUid );

int32_t lReadCertificateFromPsaIts( mbedtls_x509_crt * pxCertificateContext,
									psa_storage_uid_t xCertUid );

int32_t lReadCertificateFromPsaPS( mbedtls_x509_crt * pxCertificateContext,
								   psa_storage_uid_t xCertUid );

int lPSARandomCallback( void * pvCtx, unsigned char * pucOutput,
						size_t uxLen );

#endif /* MBEDTLS_TRANSPORT_PSA */

#endif /* MBEDTLS_TRANSPORT */
