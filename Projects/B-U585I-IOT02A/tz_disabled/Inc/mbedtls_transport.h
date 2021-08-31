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
 * @file tls_freertos.h
 * @brief TLS transport interface header.
 */

#ifndef MBEDTLS_TRANSPORT
#define MBEDTLS_TRANSPORT

/* socket definitions  */
#include "transport_interface_ext.h"

/* mbed TLS includes. */
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ssl.h"
#include "mbedtls/threading.h"
#include "mbedtls/x509.h"

/*
 * Define MBEDTLS_TRANSPORT_PKCS11_ENABLE if you intend to use a pkcs11 module for TLS.
 */
#define MBEDTLS_TRANSPORT_PKCS11_ENABLE


/*
 * Define MBEDTLS_DEBUG_C if you would like to enable mbedtls debugging
 */
/* #define MBEDTLS_DEBUG_C */

typedef enum PkiObjectForm
{
    OBJ_FORM_NONE,
    OBJ_FORM_PEM,
    OBJ_FORM_DER,
    OBJ_FORM_PKCS11_LABEL
} PkiObjectForm_t;

/**
 * @brief Contains the credentials necessary for tls connection setup.
 */
typedef struct NetworkCredentials
{
    /**
     * @brief To use ALPN, set this to a NULL-terminated list of supported
     * protocols in decreasing order of preference.
     *
     * See [this link]
     * (https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/)
     * for more information.
     */
    const char ** pAlpnProtos;

    /**
     * @brief Disable server name indication (SNI) for a TLS session.
     */
    BaseType_t disableSni;

    /**
     * @brief Form of the private key specified in #NetworkCredentials.pvPrivateKey.
     */
    PkiObjectForm_t xPrivateKeyForm;

    /**
     * @brief Pointer to a buffer containing the private key or private key PKCS#11 label.
     */
    const void * pvPrivateKey;

    /**
     * @brief Size of the private key or private key PKCS#11 label specified in
     * #NetworkCredentials.pvPrivateKey.
     */
    size_t privateKeySize;

    /**
     * @brief Form of the certificate specified in #NetworkCredentials.pvClientCert.
     */
    PkiObjectForm_t xClientCertForm;

    /**
     * @brief Pointer to a buffer containing the client certificate or PKCS#11 label.
     */
    const void * pvClientCert;

    /**
     * @brief Size of the certificate or PKCS#11 label specified in
     * #NetworkCredentials.pvClientCert.
     */
    size_t clientCertSize;

    /**
     * @brief Form of the certificate specified in #NetworkCredentials.pvRootCaCert.
     */
    PkiObjectForm_t xRootCaCertForm;

    /**
     * @brief Pointer to a buffer containing the CA certificate or PKCS#11 label.
     */
    const void * pvRootCaCert;

    /**
     * @brief Size of the certificate or PKCS#11 label specified in
     * #NetworkCredentials.pvRootCaCert.
     */
    size_t rootCaCertSize;

} NetworkCredentials_t;

/**
 * @brief TLS Connect / Disconnect return status.
 */
typedef enum TlsTransportStatus
{
    TLS_TRANSPORT_SUCCESS = 0,         /**< Function successfully completed. */
    TLS_TRANSPORT_INVALID_PARAMETER,   /**< At least one parameter was invalid. */
    TLS_TRANSPORT_INSUFFICIENT_MEMORY, /**< Insufficient memory required to establish connection. */
    TLS_TRANSPORT_INVALID_CREDENTIALS, /**< Provided credentials were invalid. */
    TLS_TRANSPORT_HANDSHAKE_FAILED,    /**< Performing TLS handshake with server failed. */
    TLS_TRANSPORT_INTERNAL_ERROR,      /**< A call to a system API resulted in an internal error. */
    TLS_TRANSPORT_CONNECT_FAILURE      /**< Initial connection to the server failed. */
} TlsTransportStatus_t;


/**
 * @brief Allocate a TLS Network Context
 *
 * @param[in] pxSocketInterface Pointer to a TransportInterfaceExtended_t for the desired lower
 *                              layer networking interface / TCP stack.
 *
 * @return pointer to a NetworkContext_t used by the TLS stack.
 */
NetworkContext_t * mbedtls_transport_allocate( const TransportInterfaceExtended_t * pxSocketInterface );


/**
 * @brief Deallocate a TLS Network Context
 *
 * @param[in] pxNetworkContext The network context to be deallocated.
 *
 */
void mbedtls_transport_free( NetworkContext_t * pxNetworkContext );


/**
 * @brief Create a TLS connection
 *
 * @param[out] pNetworkContext Pointer to a network context to contain the
 * initialized socket handle.
 * @param[in] pHostName The hostname of the remote endpoint.
 * @param[in] port The destination port.
 * @param[in] pNetworkCredentials Credentials for the TLS connection.
 * @param[in] receiveTimeoutMs Receive socket timeout.
 * @param[in] sendTimeoutMs Send socket timeout.
 *
 * @return #TLS_TRANSPORT_SUCCESS, #TLS_TRANSPORT_INSUFFICIENT_MEMORY, #TLS_TRANSPORT_INVALID_CREDENTIALS,
 * #TLS_TRANSPORT_HANDSHAKE_FAILED, #TLS_TRANSPORT_INTERNAL_ERROR, or #TLS_TRANSPORT_CONNECT_FAILURE.
 */
TlsTransportStatus_t mbedtls_transport_connect( NetworkContext_t * pxNetworkContext,
                                                const char * pHostName,
                                                uint16_t port,
                                                const NetworkCredentials_t * pNetworkCredentials,
                                                uint32_t receiveTimeoutMs,
                                                uint32_t sendTimeoutMs );

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
 * @param[in] bytesToSend Number of bytes to send from the buffer.
 *
 * @return Number of bytes (> 0) sent on success;
 * 0 if the socket times out without sending any bytes;
 * else a negative value to represent error.
 */
int32_t mbedtls_transport_send( NetworkContext_t * pxNetworkContext,
                                const void * pBuffer,
                                size_t bytesToSend );

#endif /* MBEDTLS_TRANSPORT */
