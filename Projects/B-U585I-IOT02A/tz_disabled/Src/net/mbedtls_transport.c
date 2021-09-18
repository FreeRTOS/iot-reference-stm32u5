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

/**
 * @file mbedtls_transport.c
 * @brief TLS transport interface implementations using mbedtls.
 */
#include "logging_levels.h"

#define LOG_LEVEL LOG_DEBUG

#include "logging.h"

#include "transport_interface_ext.h"
#include "mbedtls_transport.h"
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"

/* mbedTLS includes. */
#include "mbedtls/error.h"
#include "mbedtls_config.h"
#include "mbedtls/debug.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/pk_internal.h"

#include "errno.h"

/* PKCS#11 headers */
#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
#include "core_pkcs11_config.h"
#include "core_pkcs11.h"
#include "core_pki_utils.h"
#include "pkcs11.h"
#endif /* MBEDTLS_TRANSPORT_PKCS11_ENABLE */

#define MBEDTLS_DEBUG_THRESHOLD 1

/**
 * @brief Secured connection context.
 */
typedef struct TLSContext
{
    /* Lower layer */
    const TransportInterfaceExtended_t * pxSocketInterface;
    NetworkContext_t * pxSocketContext;

    /* TLS connection */
    mbedtls_ssl_config xSslConfig;              /**< @brief SSL connection configuration. */
    mbedtls_ssl_context xSslContext;            /**< @brief SSL connection context */
    mbedtls_x509_crt_profile xCertProfile;      /**< @brief Certificate security profile for this connection. */

    /* Certificates */
    mbedtls_x509_crt xRootCaCert;               /**< @brief Root CA certificate context. */
    mbedtls_x509_crt xClientCert;               /**< @brief Client certificate context. */

    /* Entropy related */
    mbedtls_entropy_context xEntropyContext;    /**< @brief Entropy context for random number generation. */
    mbedtls_ctr_drbg_context xCtrDrgbContext;   /**< @brief CTR DRBG context for random number generation. */

    mbedtls_pk_context xPrivKeyCtx;             /**< @brief Client private key context. */
    mbedtls_pk_info_t xPrivKeyInfo;             /**< @brief Client private key info. */

#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
    /* PKCS#11 interface */
    CK_FUNCTION_LIST_PTR pxP11FunctionList;     /**< @brief PKCS#11 module interface */
    CK_SESSION_HANDLE xP11Session;              /**< @brief PKCS#11 session handle */
    CK_OBJECT_HANDLE xP11PrivateKey;            /**< @brief PKCS#11 private key handle */
    CK_KEY_TYPE xKeyType;                       /**< @brief PKCS#11 private key type */
#endif
} TLSContext_t;


/*-----------------------------------------------------------*/

/**
 * @brief Represents string to be logged when mbedTLS returned error
 * does not contain a high-level code.
 */
static const char * pNoHighLevelMbedTlsCodeStr = "<No-High-Level-Code>";

/**
 * @brief Represents string to be logged when mbedTLS returned error
 * does not contain a low-level code.
 */
static const char * pNoLowLevelMbedTlsCodeStr = "<No-Low-Level-Code>";

/**
 * @brief Utility for converting the high-level code in an mbedTLS error to string,
 * if the code-contains a high-level code; otherwise, using a default string.
 */
#define mbedtlsHighLevelCodeOrDefault( mbedTlsCode )        \
    ( mbedtls_high_level_strerr( mbedTlsCode ) != NULL ) ? \
      mbedtls_high_level_strerr( mbedTlsCode ) : pNoHighLevelMbedTlsCodeStr

/**
 * @brief Utility for converting the level-level code in an mbedTLS error to string,
 * if the code-contains a level-level code; otherwise, using a default string.
 */
#define mbedtlsLowLevelCodeOrDefault( mbedTlsCode )        \
    ( mbedtls_low_level_strerr( mbedTlsCode ) != NULL ) ? \
      mbedtls_low_level_strerr( mbedTlsCode ) : pNoLowLevelMbedTlsCodeStr

/*-----------------------------------------------------------*/

/**
 * @brief Initialize the mbed TLS structures in a network connection.
 *
 * @param[in] pxTLSContext The SSL context to initialize.
 */
static void tlsContextInit( TLSContext_t * pxTLSContext );

/**
 * @brief Free the mbed TLS structures in a network connection.
 *
 * @param[in] pxTLSContext The SSL context to free.
 */
static void tlsContextFree( TLSContext_t * pxTLSContext );

/**
 * @brief Add X509 certificate to the trusted list of root certificates.
 *
 * @param[out] pxTLSContext TLS context to which the trusted server root CA is to be added.
 * @param[in] pNetworkCredentials NetworkCredentials object referencing the root CA certificate.
 *
 * @return 0 on success; otherwise, failure;
 */
static int32_t initRootCa( TLSContext_t * pxTLSContext,
                           const NetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Set X509 certificate as client certificate for the server to authenticate.
 *
 * @param[out] pxTLSContext SSL context to which the client certificate is to be set.
 * @param[in] pNetworkCredentials NetworkCredentials object referencing the client certificate.
 *
 * @return 0 on success; otherwise, failure;
 */
static int32_t initClientCertificate( TLSContext_t * pxTLSContext,
                                      const NetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Initialize the private key from a NetworkCredentials_t struct.
 *
 * @param[out] pxTLSContext SSL context to which the private key is to be set.
 * @param[in] pNetworkCredentials NetworkCredentials object referencing the private key.
 *
 * @return 0 on success; otherwise, failure;
 */
static int32_t initPrivateKey( TLSContext_t * pxTLSContext,
                               const NetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Passes TLS credentials to the mbedtls library.
 *
 * Provides the root CA certificate, client certificate, and private key to the
 * mbedtls library. If the client certificate or private key is not NULL, mutual
 * authentication is used when performing the TLS handshake.
 *
 * @param[out] pxTLSContext SSL context to which the credentials are to be imported.
 * @param[in] pNetworkCredentials TLS credentials to be imported.
 *
 * @return 0 on success; otherwise, failure;
 */
static int32_t setCredentials( TLSContext_t * pxTLSContext,
                               const NetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Set optional configurations for the TLS connection.
 *
 * This function is used to set SNI and ALPN protocols.
 *
 * @param[in] pxTLSContext SSL context to which the optional configurations are to be set.
 * @param[in] pHostName Remote host name, used for server name indication.
 * @param[in] pNetworkCredentials TLS setup parameters.
 */
static void setOptionalConfigurations( TLSContext_t * pxTLSContext,
                                       const char * pHostName,
                                       const NetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Setup TLS by initializing contexts and setting configurations.
 *
 * @param[in] pxNetworkContext Network context.
 * @param[in] pHostName Remote host name, used for server name indication.
 * @param[in] pNetworkCredentials TLS setup parameters.
 *
 * @return #TLS_TRANSPORT_SUCCESS, #TLS_TRANSPORT_INSUFFICIENT_MEMORY, #TLS_TRANSPORT_INVALID_CREDENTIALS,
 * or #TLS_TRANSPORT_INTERNAL_ERROR.
 */
static TlsTransportStatus_t tlsSetup( TLSContext_t * pxTLSContext,
                                      const char * pHostName,
                                      const NetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Perform the TLS handshake on a TCP connection.
 *
 * @param[in] pxNetworkContext Network context.
 * @param[in] pNetworkCredentials TLS setup parameters.
 *
 * @return #TLS_TRANSPORT_SUCCESS, #TLS_TRANSPORT_HANDSHAKE_FAILED, or #TLS_TRANSPORT_INTERNAL_ERROR.
 */
static TlsTransportStatus_t tlsHandshake( TLSContext_t * pxTLSContext,
                                          const NetworkCredentials_t * pNetworkCredentials );

/**
 * @brief Initialize mbedTLS.
 *
 * @param[out] entropyContext mbed TLS entropy context for generation of random numbers.
 * @param[out] ctrDrgbContext mbed TLS CTR DRBG context for generation of random numbers.
 *
 * @return #TLS_TRANSPORT_SUCCESS, or #TLS_TRANSPORT_INTERNAL_ERROR.
 */
static TlsTransportStatus_t initMbedtls( mbedtls_entropy_context * pExntropyContext,
                                         mbedtls_ctr_drbg_context * pxCtrDrgbContext );

#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE

/**
 * @brief Helper for reading the specified certificate object, if present,
 * out of storage, into RAM, and then into an mbedTLS certificate context
 * object.
 *
 * @param[in] pxTLSContext Caller TLS context.
 * @param[in] pcLabel PKCS #11 certificate object label.
 * @param[out] pxCertificateContext Certificate context.
 *
 * @return Zero on success.
 */
static CK_RV readCertificateFromPKCS11( TLSContext_t * pxTLSContext,
                                        const char * pcLabel,
                                        mbedtls_x509_crt * pxCertificateContext );

#endif /* MBEDTLS_TRANSPORT_PKCS11_ENABLE */

#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE

/**
 * @brief Helper for reading the specified certificate object, if present,
 * out of storage, into RAM, and then into an mbedTLS certificate context
 * object.
 *
 * @param[in] pxTLSContext Caller TLS context.
 * @param[in] pcLabel PKCS #11 key object label.
 *
 * @return Zero on success.
 */
static CK_RV validatePrivateKeyPKCS11( TLSContext_t * pxTLSContext,
                                       const char * pcLabel );

#endif /* MBEDTLS_TRANSPORT_PKCS11_ENABLE */

#ifdef MBEDTLS_DEBUG_C
    /* Used to print mbedTLS log output. */
    static void vTLSDebugPrint( void *ctx, int level, const char *file, int line, const char *str );
#endif

/*-----------------------------------------------------------*/

static void tlsContextInit( TLSContext_t * pxTLSContext )
{
    configASSERT( pxTLSContext != NULL );

    mbedtls_ssl_config_init( &( pxTLSContext->xSslConfig ) );
    mbedtls_x509_crt_init( &( pxTLSContext->xRootCaCert ) );
    mbedtls_x509_crt_init( &( pxTLSContext->xClientCert ) );
    mbedtls_ssl_init( &( pxTLSContext->xSslContext ) );
    mbedtls_pk_init( &( pxTLSContext->xPrivKeyCtx ) );

#ifdef MBEDTLS_DEBUG_C
    mbedtls_ssl_conf_dbg( &( pxTLSContext->xSslConfig ), vTLSDebugPrint, NULL );
    mbedtls_debug_set_threshold( MBEDTLS_DEBUG_THRESHOLD );
#endif  /* MBEDTLS_DEBUG_C */

#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
    xInitializePkcs11Session( &( pxTLSContext->xP11Session ) );
    C_GetFunctionList( &( pxTLSContext->pxP11FunctionList ) );
#endif  /* MBEDTLS_TRANSPORT_PKCS11_ENABLE */

    /* Prevent compiler warnings when LogDebug() is defined away. */
    ( void ) pNoLowLevelMbedTlsCodeStr;
    ( void ) pNoHighLevelMbedTlsCodeStr;
}
/*-----------------------------------------------------------*/

static void tlsContextFree( TLSContext_t * pxTLSContext )
{
    configASSERT( pxTLSContext != NULL );

    mbedtls_ssl_free( &( pxTLSContext->xSslContext ) );
    mbedtls_x509_crt_free( &( pxTLSContext->xRootCaCert ) );
    mbedtls_x509_crt_free( &( pxTLSContext->xClientCert ) );
    mbedtls_ssl_config_free( &( pxTLSContext->xSslConfig ) );
    mbedtls_pk_free( &( pxTLSContext->xPrivKeyCtx ) );


#ifndef MBEDTLS_TRANSPORT_PKCS11_RANDOM
    mbedtls_entropy_free( &( pxTLSContext->xEntropyContext ) );
    mbedtls_ctr_drbg_free( &( pxTLSContext->xCtrDrgbContext ) );
#else

#endif

#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
    configASSERT( pxTLSContext->pxP11FunctionList != NULL );
    configASSERT( pxTLSContext->pxP11FunctionList->C_CloseSession != NULL );
    configASSERT( pxTLSContext->xP11Session != 0 );

    pxTLSContext->pxP11FunctionList->C_CloseSession( pxTLSContext->xP11Session );
#endif


}
/*-----------------------------------------------------------*/

static int32_t initRootCa( TLSContext_t * pxTLSContext,
                           const NetworkCredentials_t * pNetworkCredentials)
{
    int32_t lError = -1;

    configASSERT( pxTLSContext != NULL );
    configASSERT( pNetworkCredentials != NULL );
    configASSERT( pNetworkCredentials->rootCaCertSize > 0 );
    configASSERT( pNetworkCredentials->pvRootCaCert != NULL );

    switch( pNetworkCredentials->xRootCaCertForm )
    {
    case OBJ_FORM_PEM:
        /* Intentional fall through */
    case OBJ_FORM_DER:
    {
        lError = mbedtls_x509_crt_parse( &( pxTLSContext->xRootCaCert ),
                                         pNetworkCredentials->pvRootCaCert,
                                         pNetworkCredentials->rootCaCertSize );
        if( lError != 0 )
        {
            LogError( "Failed to parse server root CA certificate: mbedTLSError= %s : %s.",
                        mbedtlsHighLevelCodeOrDefault( lError ),
                        mbedtlsLowLevelCodeOrDefault( lError ) );
        }
        break;
    }
    case OBJ_FORM_PKCS11_LABEL:
#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
    {
        lError = readCertificateFromPKCS11( pxTLSContext,
                                            pNetworkCredentials->pvRootCaCert,
                                            &( pxTLSContext->xRootCaCert ) );
        if( lError != 0 )
        {
            LogError( "Failed to read server root CA certificate from PKCS11 module. xResult= %d",
                      lError );
        }
        break;
    }
#endif
    /* Intentional fall through when MBEDTLS_TRANSPORT_PKCS11_ENABLE is not defined */
    case OBJ_FORM_NONE:
        /* Intentional fall through */
    default:
        LogError( "Invalid root CA certificate form specified in xRootCaCertForm." );
        lError = TLS_TRANSPORT_INVALID_PARAMETER;
        break;

    }

    if( lError == 0 )
    {
        mbedtls_ssl_conf_ca_chain( &( pxTLSContext->xSslConfig ),
                                   &( pxTLSContext->xRootCaCert ),
                                   NULL );
    }

    return lError;
}
/*-----------------------------------------------------------*/

static int32_t initClientCertificate( TLSContext_t * pxTLSContext,
                                      const NetworkCredentials_t * pNetworkCredentials )
{
    int32_t mbedtlsError = -1;

    configASSERT( pxTLSContext != NULL );
    configASSERT( pNetworkCredentials != NULL );



    /* Setup the client certificate. */
    mbedtlsError = mbedtls_x509_crt_parse( &( pxTLSContext->xClientCert ),
                                           pNetworkCredentials->pvClientCert,
                                           pNetworkCredentials->clientCertSize );

    if( mbedtlsError != 0 )
    {
        LogError( "Failed to parse the client certificate: mbedTLSError= %s : %s.",
                    mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                    mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );
    }

    return mbedtlsError;
}
/*-----------------------------------------------------------*/

static int32_t initPrivateKey( TLSContext_t * pxTLSContext,
                               const NetworkCredentials_t * pNetworkCredentials )
{
    int32_t lError = -1;

    configASSERT( pxTLSContext != NULL );
    configASSERT( pNetworkCredentials != NULL );
    configASSERT( pNetworkCredentials->privateKeySize > 0 );
    configASSERT( pNetworkCredentials->pvPrivateKey != NULL );

    switch( pNetworkCredentials->xPrivateKeyForm )
    {
    case OBJ_FORM_PEM:
        /* Intentional fall through */
    case OBJ_FORM_DER:
    {
        configASSERT( pNetworkCredentials->privateKeySize > 0 );
        configASSERT( pNetworkCredentials->pvPrivateKey != NULL );

        lError = mbedtls_pk_parse_key( &( pxTLSContext->xPrivKeyCtx ),
                                       ( const unsigned char * ) pNetworkCredentials->pvPrivateKey,
                                       pNetworkCredentials->privateKeySize,
                                       NULL,
                                       0 );
        if( lError != 0 )
        {
            LogError( "Failed to parse the client key: lError= %s : %s.",
                        mbedtlsHighLevelCodeOrDefault( lError ),
                        mbedtlsLowLevelCodeOrDefault( lError ) );
        }
        break;
    }
    case OBJ_FORM_PKCS11_LABEL:
#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
    {
        configASSERT( pNetworkCredentials->privateKeySize < pkcs11configMAX_LABEL_LENGTH );

        lError = validatePrivateKeyPKCS11( pxTLSContext,
                                           ( const char * ) pNetworkCredentials->pvPrivateKey );
    }
#else
        lError = TLS_TRANSPORT_INVALID_PARAMETER;
        LogError( "xPrivateKeyForm was specified as OBJ_FORM_PKCS11_LABEL, however MBEDTLS_TRANSPORT_PKCS11_ENABLE has not been defined." );
#endif
        break;
    case OBJ_FORM_NONE:
        /* Intentional fallthrough */
    default:
        lError = TLS_TRANSPORT_INVALID_PARAMETER;
        break;
    }

    return lError;
}
/*-----------------------------------------------------------*/

static int32_t setCredentials( TLSContext_t * pxTLSContext,
                               const NetworkCredentials_t * pNetworkCredentials )
{
    int32_t lError = -1;

    configASSERT( pxTLSContext != NULL );
    configASSERT( pNetworkCredentials != NULL );

    /* Set up the certificate security profile, starting from the default value. */
    pxTLSContext->xCertProfile = mbedtls_x509_crt_profile_default;

    /* Set SSL authmode and the RNG context. */
//    mbedtls_ssl_conf_authmode( &( pxTLSContext->xSslConfig ),
//                               MBEDTLS_SSL_VERIFY_REQUIRED );

    //TODO FIXME Skipping CA certificate verification
    mbedtls_ssl_conf_authmode( &( pxTLSContext->xSslConfig ),
                                MBEDTLS_SSL_VERIFY_NONE );


    mbedtls_ssl_conf_rng( &( pxTLSContext->xSslConfig ),
                          mbedtls_ctr_drbg_random,
                          &( pxTLSContext->xCtrDrgbContext ) );

    mbedtls_ssl_conf_cert_profile( &( pxTLSContext->xSslConfig ),
                                   &( pxTLSContext->xCertProfile ) );
    //TODO Random sourced from pkcs11

    lError = initRootCa( pxTLSContext,
                         pNetworkCredentials );

    if( ( pNetworkCredentials->pvClientCert != NULL ) &&
        ( pNetworkCredentials->clientCertSize > 0 ) &&
        ( pNetworkCredentials->xClientCertForm != OBJ_FORM_NONE ) &&
        ( pNetworkCredentials->pvPrivateKey != NULL ) &&
        ( pNetworkCredentials->privateKeySize > 0 ) &&
        ( pNetworkCredentials->xPrivateKeyForm != OBJ_FORM_NONE ) )
    {
        if( lError == 0 )
        {
            lError = initClientCertificate( pxTLSContext,
                                            pNetworkCredentials );
            configASSERT( lError == 0 );

        }

        if( lError == 0 )
        {
            lError = initPrivateKey( pxTLSContext,
                                     pNetworkCredentials );
            configASSERT( lError == 0 );
        }

        if( lError == 0 )
        {
            lError = mbedtls_ssl_conf_own_cert( &( pxTLSContext->xSslConfig ),
                                                      &( pxTLSContext->xClientCert ),
                                                      &( pxTLSContext->xPrivKeyCtx ) );
            configASSERT( lError == 0 );
        }
    }

    return lError;
}
/*-----------------------------------------------------------*/

static void setOptionalConfigurations( TLSContext_t * pxTLSContext,
                                       const char * pHostName,
                                       const NetworkCredentials_t * pNetworkCredentials )
{
    int32_t mbedtlsError = -1;

    configASSERT( pxTLSContext != NULL );
    configASSERT( pHostName != NULL );
    configASSERT( pNetworkCredentials != NULL );

    if( pNetworkCredentials->pAlpnProtos != NULL )
    {
        /* Include an application protocol list in the TLS ClientHello
         * message. */
        mbedtlsError = mbedtls_ssl_conf_alpn_protocols( &( pxTLSContext->xSslConfig ),
                                                        pNetworkCredentials->pAlpnProtos );

        if( mbedtlsError != 0 )
        {
            LogError( "Failed to configure ALPN protocol in mbed TLS: mbedTLSError= %s : %s.",
                        mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                        mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );
        }
    }

    /* Enable SNI if requested. */
    if( pNetworkCredentials->disableSni == pdFALSE )
    {
        mbedtlsError = mbedtls_ssl_set_hostname( &( pxTLSContext->xSslContext ),
                                                 pHostName );

        if( mbedtlsError != 0 )
        {
            LogError( "Failed to set server name: mbedTLSError= %s : %s.",
                        mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                        mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );
        }
    }

    /* Set Maximum Fragment Length if enabled. */
    #ifdef MBEDTLS_SSL_MAX_FRAGMENT_LENGTH

        /* Enable the max fragment extension. 4096 bytes is currently the largest fragment size permitted.
         * See RFC 8449 https://tools.ietf.org/html/rfc8449 for more information.
         *
         * Smaller values can be found in "mbedtls/include/ssl.h".
         */
        mbedtlsError = mbedtls_ssl_conf_max_frag_len( &( pxTLSContext->xSslConfig ), MBEDTLS_SSL_MAX_FRAG_LEN_4096 );

        if( mbedtlsError != 0 )
        {
            LogError( "Failed to maximum fragment length extension: mbedTLSError= %s : %s.",
                      mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                      mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );
        }
    #endif /* ifdef MBEDTLS_SSL_MAX_FRAGMENT_LENGTH */
}

/*-----------------------------------------------------------*/

static TlsTransportStatus_t tlsSetup( TLSContext_t * pxTLSContext,
                                      const char * pHostName,
                                      const NetworkCredentials_t * pNetworkCredentials )
{
    TlsTransportStatus_t returnStatus = TLS_TRANSPORT_SUCCESS;
    int32_t mbedtlsError = 0;

    configASSERT( pxTLSContext != NULL );
    configASSERT( pHostName != NULL );
    configASSERT( pNetworkCredentials != NULL );
    configASSERT( pNetworkCredentials->pvRootCaCert != NULL );

    /* Initialize the mbed TLS context structures. */
    tlsContextInit( pxTLSContext );

    mbedtlsError = mbedtls_ssl_config_defaults( &( pxTLSContext->xSslConfig ),
                                                MBEDTLS_SSL_IS_CLIENT,
                                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                                MBEDTLS_SSL_PRESET_DEFAULT );

    if( mbedtlsError != 0 )
    {
        LogError( "Failed to set default SSL configuration: mbedTLSError= %s : %s.",
                    mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                    mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );

        /* Per mbed TLS docs, mbedtls_ssl_config_defaults only fails on memory allocation. */
        returnStatus = TLS_TRANSPORT_INSUFFICIENT_MEMORY;
    }

    if( returnStatus == TLS_TRANSPORT_SUCCESS )
    {
        mbedtlsError = setCredentials( pxTLSContext,
                                       pNetworkCredentials );

        if( mbedtlsError != 0 )
        {
            returnStatus = TLS_TRANSPORT_INVALID_CREDENTIALS;
        }
        else
        {
            /* Optionally set SNI and ALPN protocols. */
            setOptionalConfigurations( pxTLSContext,
                                       pHostName,
                                       pNetworkCredentials );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int mbedtls_ssl_send( void * pvCtx,
                             const unsigned char * pcBuf,
                             size_t xLen )
{
    TLSContext_t * pxTLSContext = ( TLSContext_t * ) pvCtx;
    const TransportInterfaceExtended_t * pxSock = pxTLSContext->pxSocketInterface;
    int lReturnValue = 0;

    lReturnValue = pxSock->send( pxTLSContext->pxSocketContext,
                                 ( void * const ) pcBuf,
                                 xLen );

    if( lReturnValue < 0 )
    {
        /* force use of newlibc errno */
        switch( *__errno() )
        {
#if EAGAIN != EWOULDBLOCK
        case EAGAIN:
#endif
        case EINTR:
        case EWOULDBLOCK:
            lReturnValue = MBEDTLS_ERR_SSL_WANT_WRITE;
            break;
        case EPIPE:
        case ECONNRESET:
            lReturnValue = MBEDTLS_ERR_NET_CONN_RESET;
            break;
        default:
            lReturnValue = MBEDTLS_ERR_NET_SEND_FAILED;
            break;
        }
    }
    return lReturnValue;
}

/*-----------------------------------------------------------*/

static int mbedtls_ssl_recv( void * pvCtx,
                             unsigned char * pcBuf,
                             size_t xLen )
{
    TLSContext_t * pxTLSContext = ( TLSContext_t * ) pvCtx;
    const TransportInterfaceExtended_t * pxSock = pxTLSContext->pxSocketInterface;
    int lReturnValue = 0;

    lReturnValue = pxSock->recv( pxTLSContext->pxSocketContext,
                                 ( void * ) pcBuf,
                                 xLen );

    if( lReturnValue < 0 )
    {
        /* force use of newlibc errno */
        switch( *__errno() )
        {
#if EAGAIN != EWOULDBLOCK
        case EAGAIN:
#endif
        case EINTR:
        case EWOULDBLOCK:
            lReturnValue = MBEDTLS_ERR_SSL_WANT_READ;
            break;
        case EPIPE:
        case ECONNRESET:
            lReturnValue = MBEDTLS_ERR_NET_CONN_RESET;
            break;
        default:
            lReturnValue = MBEDTLS_ERR_NET_RECV_FAILED;
            break;
        }
    }
    return lReturnValue;
}

/*-----------------------------------------------------------*/

static TlsTransportStatus_t tlsHandshake( TLSContext_t * pxTLSContext,
                                          const NetworkCredentials_t * pNetworkCredentials )
{
    TlsTransportStatus_t returnStatus = TLS_TRANSPORT_SUCCESS;
    int32_t mbedtlsError = 0;

    configASSERT( pxTLSContext != NULL );
    configASSERT( pNetworkCredentials != NULL );

    /* Initialize the mbed TLS secured connection context. */
    mbedtlsError = mbedtls_ssl_setup( &( pxTLSContext->xSslContext ),
                                      &( pxTLSContext->xSslConfig ) );

    if( mbedtlsError != 0 )
    {
        LogError( "Failed to set up mbed TLS SSL context: mbedTLSError= %s : %s.",
                    mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                    mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );

        returnStatus = TLS_TRANSPORT_INTERNAL_ERROR;
    }
    else
    {
        /* Set the underlying IO for the TLS connection. */

        /* MISRA Rule 11.2 flags the following line for casting the second
         * parameter to void *. This rule is suppressed because
         * #mbedtls_ssl_set_bio requires the second parameter as void *.
         */
        /* coverity[misra_c_2012_rule_11_2_violation] */
        mbedtls_ssl_set_bio( &( pxTLSContext->xSslContext ),
                             ( void * ) pxTLSContext,
                             mbedtls_ssl_send,
                             mbedtls_ssl_recv,
                             NULL );
    }

    if( returnStatus == TLS_TRANSPORT_SUCCESS )
    {
        /* Perform the TLS handshake. */
        do
        {
            mbedtlsError = mbedtls_ssl_handshake( &( pxTLSContext->xSslContext ) );
        }
        while( ( mbedtlsError == MBEDTLS_ERR_SSL_WANT_READ ) ||
               ( mbedtlsError == MBEDTLS_ERR_SSL_WANT_WRITE ) );

        if( mbedtlsError != 0 )
        {
            LogError( "Failed to perform TLS handshake: mbedTLSError= %s : %s.",
                        mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                        mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );

            returnStatus = TLS_TRANSPORT_HANDSHAKE_FAILED;
        }
        else
        {
            LogInfo( "(Network connection %p) TLS handshake successful.",
                     pxTLSContext );
        }
    }

    return returnStatus;
}
/*-----------------------------------------------------------*/

static TlsTransportStatus_t initMbedtls( mbedtls_entropy_context * pEntropyContext,
                                         mbedtls_ctr_drbg_context * pCtrDrgbContext )
{
    TlsTransportStatus_t returnStatus = TLS_TRANSPORT_SUCCESS;
    int32_t mbedtlsError = 0;

    /* Set the mutex functions for mbed TLS thread safety. */
    mbedtls_threading_set_alt( mbedtls_platform_mutex_init,
                               mbedtls_platform_mutex_free,
                               mbedtls_platform_mutex_lock,
                               mbedtls_platform_mutex_unlock );

    /* Initialize contexts for random number generation. */
    mbedtls_entropy_init( pEntropyContext );
    mbedtls_ctr_drbg_init( pCtrDrgbContext );

    /* Add a strong entropy source. At least one is required. */
    /* Added by STM32-specific rng_alt.c implementation */


    if( mbedtlsError != 0 )
    {
        LogError( "Failed to add entropy source: mbedTLSError= %s : %s.",
                  mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                  mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );
        returnStatus = TLS_TRANSPORT_INTERNAL_ERROR;
    }

    if( returnStatus == TLS_TRANSPORT_SUCCESS )
    {
        /* Seed the random number generator. */
        mbedtlsError = mbedtls_ctr_drbg_seed( pCtrDrgbContext,
                                              mbedtls_entropy_func,
                                              pEntropyContext,
                                              NULL,
                                              0 );

        if( mbedtlsError != 0 )
        {
            LogError( "Failed to seed PRNG: mbedTLSError= %s : %s.",
                      mbedtlsHighLevelCodeOrDefault( mbedtlsError ),
                      mbedtlsLowLevelCodeOrDefault( mbedtlsError ) );
            returnStatus = TLS_TRANSPORT_INTERNAL_ERROR;
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
static CK_RV readCertificateFromPKCS11( TLSContext_t * pxTLSContext,
                                        const char * pcLabel,
                                        mbedtls_x509_crt * pxCertificateContext )
{
    CK_RV xResult = CKR_OK;
    CK_ATTRIBUTE xTemplate = { 0 };
    CK_OBJECT_HANDLE xCertObj = 0;

    /* Get the handle of the certificate. */
    xResult = xFindObjectWithLabelAndClass( pxTLSContext->xP11Session,
                                            pcLabel,
                                            strnlen( pcLabel, pkcs11configMAX_LABEL_LENGTH - 1 ),
                                            CKO_CERTIFICATE,
                                            &xCertObj );

    if( ( CKR_OK == xResult ) && ( xCertObj == CK_INVALID_HANDLE ) )
    {
        xResult = CKR_OBJECT_HANDLE_INVALID;
    }

    /* Query the certificate size. */
    if( CKR_OK == xResult )
    {
        xTemplate.type = CKA_VALUE;
        xTemplate.ulValueLen = 0;
        xTemplate.pValue = NULL;
        xResult = pxTLSContext->pxP11FunctionList->C_GetAttributeValue( pxTLSContext->xP11Session,
                                                                       xCertObj,
                                                                       &xTemplate,
                                                                       1 );
    }

    /* Create a buffer for the certificate. */
    if( CKR_OK == xResult )
    {
        xTemplate.pValue = pvPortMalloc( xTemplate.ulValueLen );

        if( NULL == xTemplate.pValue )
        {
            xResult = CKR_HOST_MEMORY;
        }
    }

    /* Export the certificate. */
    if( CKR_OK == xResult )
    {
        xResult = pxTLSContext->pxP11FunctionList->C_GetAttributeValue( pxTLSContext->xP11Session,
                                                                       xCertObj,
                                                                       &xTemplate,
                                                                       1 );
    }

    /* Decode the certificate. */
    if( CKR_OK == xResult )
    {
        xResult = mbedtls_x509_crt_parse( pxCertificateContext,
                                          ( const unsigned char * ) xTemplate.pValue,
                                          xTemplate.ulValueLen );
    }

    /* Free memory. */
    vPortFree( xTemplate.pValue );

    return xResult;
}
#endif /* MBEDTLS_TRANSPORT_PKCS11_ENABLE */

/*-----------------------------------------------------------*/

#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
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
    TLSContext_t * pxTLSContext = ( TLSContext_t * ) pvContext;
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
    if( CKK_RSA == pxTLSContext->xKeyType )
    {
        xMech.mechanism = CKM_RSA_PKCS;

        /* mbedTLS expects hashed data without padding, but PKCS #11 C_Sign function performs a hash
         * & sign if hash algorithm is specified.  This helper function applies padding
         * indicating data was hashed with SHA-256 while still allowing pre-hashed data to
         * be provided. */
        xResult = vAppendSHA256AlgorithmIdentifierSequence( ( uint8_t * ) pucHash, xToBeSigned );
        xToBeSignedLen = pkcs11RSA_SIGNATURE_INPUT_LENGTH;
    }
    else if( CKK_EC == pxTLSContext->xKeyType )
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
        xResult = pxTLSContext->pxP11FunctionList->C_SignInit( pxTLSContext->xP11Session,
                                                               &xMech,
                                                               pxTLSContext->xP11PrivateKey );
    }

    if( CKR_OK == xResult )
    {
        *pxSigLen = sizeof( xToBeSigned );
        xResult = pxTLSContext->pxP11FunctionList->C_Sign( ( CK_SESSION_HANDLE ) pxTLSContext->xP11Session,
                                                           xToBeSigned,
                                                           xToBeSignedLen,
                                                           pucSig,
                                                           ( CK_ULONG_PTR ) pxSigLen );
    }

    if( ( xResult == CKR_OK ) && ( CKK_EC == pxTLSContext->xKeyType ) )
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
#endif /* MBEDTLS_TRANSPORT_PKCS11_ENABLE */

/*-----------------------------------------------------------*/

#ifdef MBEDTLS_TRANSPORT_PKCS11_ENABLE
static CK_RV validatePrivateKeyPKCS11( TLSContext_t * pxCtx,
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

        pxCtx->xPrivKeyCtx.pk_info = &pxCtx->xPrivKeyInfo;
        pxCtx->xPrivKeyCtx.pk_ctx = pxCtx;
    }

    /* Free memory. */
    vPortFree( pxSlotIds );

    return xResult;
}
#endif /* MBEDTLS_TRANSPORT_PKCS11_ENABLE */

/*-----------------------------------------------------------*/

NetworkContext_t * mbedtls_transport_allocate( const TransportInterfaceExtended_t * pxSocketInterface )
{
    TLSContext_t * pxTLSContext;

    if( pxSocketInterface == NULL )
    {
        LogError( "The given pxSocketInterface parameter is NULL." );
    }
    else
    {
        pxTLSContext = ( TLSContext_t * ) pvPortMalloc( sizeof( TLSContext_t ) );
    }


    if( pxTLSContext == NULL )
    {
        LogError( "Could not allocate memory for TLSContext_t." );
    }
    else
    {
        memset( pxTLSContext, 0, sizeof( TLSContext_t ) );
        pxTLSContext->pxSocketInterface = pxSocketInterface;
    }
    return ( NetworkContext_t * ) pxTLSContext;
}

/*-----------------------------------------------------------*/

void mbedtls_transport_free( NetworkContext_t * pxNetworkContext )
{
    vPortFree( pxNetworkContext );
}

/*-----------------------------------------------------------*/

TlsTransportStatus_t mbedtls_transport_connect( NetworkContext_t * pxNetworkContext,
                                                const char * pHostName,
                                                uint16_t port,
                                                const NetworkCredentials_t * pNetworkCredentials,
                                                uint32_t receiveTimeoutMs,
                                                uint32_t sendTimeoutMs )
{
    TlsTransportStatus_t returnStatus = TLS_TRANSPORT_SUCCESS;
    BaseType_t socketError = 0;
    TLSContext_t * pxTLSContext = ( TLSContext_t * ) pxNetworkContext;

    configASSERT( pxTLSContext != NULL );

    const TransportInterfaceExtended_t * pxSocketInterface = pxTLSContext->pxSocketInterface;

    configASSERT( pxSocketInterface != NULL );

    if( ( pxNetworkContext == NULL ) ||
        ( pHostName == NULL ) ||
        ( pNetworkCredentials == NULL ) )
    {
        LogError( "Invalid input parameter(s): Arguments cannot be NULL. pxNetworkContext=%p, "
                  "pHostName=%p, pNetworkCredentials=%p.",
                  pxNetworkContext,
                  pHostName,
                  pNetworkCredentials );
        returnStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else if( pNetworkCredentials->pvRootCaCert == NULL )
    {
        LogError( "pvRootCaCert cannot be NULL." );
        returnStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else
    {
        /* Empty else for MISRA 15.7 compliance. */
    }

    /* Disconnect socket if has previously been connected */
    if( pxTLSContext->pxSocketContext != NULL )
    {
        configASSERT( pxTLSContext->pxSocketInterface != NULL );
        pxTLSContext->pxSocketInterface->close( pxTLSContext->pxSocketContext );
        pxTLSContext->pxSocketContext = NULL;
    }

    /* Allocate a new socket */
    pxTLSContext->pxSocketContext = pxSocketInterface->socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    if( pxTLSContext->pxSocketContext == NULL )
    {
        LogError( "Error when allocating socket" );
        returnStatus = TLS_TRANSPORT_INTERNAL_ERROR;
    }

    /* Set send and receive timeout parameters */
    if( returnStatus == TLS_TRANSPORT_SUCCESS )
    {

        socketError |= pxSocketInterface->setsockopt( pxTLSContext->pxSocketContext,
                                                      SO_RCVTIMEO,
                                                      (void *)&receiveTimeoutMs,
                                                      sizeof( receiveTimeoutMs ) );

        socketError |= pxSocketInterface->setsockopt( pxTLSContext->pxSocketContext,
                                                      SO_SNDTIMEO,
                                                      (void *)&sendTimeoutMs,
                                                      sizeof(sendTimeoutMs) );

        if( socketError != SOCK_OK )
        {
            LogError( "Failed to set socket options SO_RCVTIMEO or SO_SNDTIMEO." );
            returnStatus = TLS_TRANSPORT_INVALID_PARAMETER;
        }
    }

    /* Establish a TCP connection with the server. */
    if( returnStatus == TLS_TRANSPORT_SUCCESS )
    {

        socketError = pxSocketInterface->connect_name( pxTLSContext->pxSocketContext,
                                                       pHostName,
                                                       port );
        if( socketError != SOCK_OK )
        {
            LogError( "Failed to connect to %s with error %d.",
                        pHostName,
                        socketError );
            returnStatus = TLS_TRANSPORT_CONNECT_FAILURE;
        }
    }

    /* Initialize mbedtls. */
    if( returnStatus == TLS_TRANSPORT_SUCCESS )
    {
        returnStatus = initMbedtls( &( pxTLSContext->xEntropyContext ),
                                    &( pxTLSContext->xCtrDrgbContext ) );
    }

    /* Initialize TLS contexts and set credentials. */
    if( returnStatus == TLS_TRANSPORT_SUCCESS )
    {
        returnStatus = tlsSetup( pxTLSContext, pHostName, pNetworkCredentials );
    }

    /* Perform TLS handshake. */
    if( returnStatus == TLS_TRANSPORT_SUCCESS )
    {
        returnStatus = tlsHandshake( pxTLSContext, pNetworkCredentials );
    }

    /* Clean up on failure. */
    if( returnStatus != TLS_TRANSPORT_SUCCESS )
    {
        if( pxNetworkContext != NULL )
        {
            tlsContextFree( pxTLSContext );

            if( pxTLSContext->pxSocketContext != NULL )
            {
                /* Call socket close function to deallocate the socket. */
                pxSocketInterface->close( pxTLSContext->pxSocketContext );
                pxTLSContext->pxSocketContext = NULL;
            }
        }
    }
    else
    {
        LogInfo( "(Network connection %p) Connection to %s established.",
                   pxNetworkContext,
                   pHostName );
    }

    return returnStatus;
}
/*-----------------------------------------------------------*/

void mbedtls_transport_disconnect( NetworkContext_t * pxNetworkContext )
{
    BaseType_t tlsStatus = 0;
    TLSContext_t * pxTLSContext = ( TLSContext_t * ) pxNetworkContext;

    configASSERT( pxNetworkContext != NULL );

    if( pxNetworkContext != NULL )
    {
        /* Attempting to terminate TLS connection. */
        tlsStatus = ( BaseType_t ) mbedtls_ssl_close_notify( &( pxTLSContext->xSslContext ) );

        /* Ignore the WANT_READ and WANT_WRITE return values. */
        if( ( tlsStatus != ( BaseType_t ) MBEDTLS_ERR_SSL_WANT_READ ) &&
            ( tlsStatus != ( BaseType_t ) MBEDTLS_ERR_SSL_WANT_WRITE ) )
        {
            if( tlsStatus == 0 )
            {
                LogInfo( "(Network connection %p) TLS close-notify sent.",
                           pxNetworkContext );
            }
            else
            {
                LogError( "(Network connection %p) Failed to send TLS close-notify: mbedTLSError= %s : %s.",
                            pxNetworkContext,
                            mbedtlsHighLevelCodeOrDefault( tlsStatus ),
                            mbedtlsLowLevelCodeOrDefault( tlsStatus ) );
            }
        }
        else
        {
//            /* WANT_READ and WANT_WRITE can be ignored. Logging for debugging purposes. */
//            LogInfo( "(Network connection %p) TLS close-notify sent; "
//                       "received %s as the TLS status can be ignored for close-notify."
//                       ( tlsStatus == MBEDTLS_ERR_SSL_WANT_READ ) ? "WANT_READ" : "WANT_WRITE",
//                       pxNetworkContext );
        }

        if( pxTLSContext->pxSocketContext != NULL &&
            pxTLSContext->pxSocketInterface != NULL )
        {
            /* Call socket close function to deallocate the socket. */
            pxTLSContext->pxSocketInterface->close( pxTLSContext->pxSocketContext );
            pxTLSContext->pxSocketContext = NULL;
        }

        /* Free mbedTLS contexts. */
        tlsContextFree( pxTLSContext );
    }

    /* Clear the mutex functions for mbed TLS thread safety. */
    mbedtls_threading_free_alt();
}
/*-----------------------------------------------------------*/

int32_t mbedtls_transport_recv( NetworkContext_t * pxNetworkContext,
                                void * pBuffer,
                                size_t bytesToRecv )
{
    int32_t tlsStatus = 0;
    TLSContext_t * pxTLSContext = ( TLSContext_t * ) pxNetworkContext;

    configASSERT( pxNetworkContext != NULL );
    configASSERT( pBuffer != NULL );
    configASSERT( bytesToRecv > 0 );

    tlsStatus = ( int32_t ) mbedtls_ssl_read( &( pxTLSContext->xSslContext ),
                                              pBuffer,
                                              bytesToRecv );

    if( ( tlsStatus == MBEDTLS_ERR_SSL_TIMEOUT ) ||
        ( tlsStatus == MBEDTLS_ERR_SSL_WANT_READ ) ||
        ( tlsStatus == MBEDTLS_ERR_SSL_WANT_WRITE ) )
    {
        /* Mark these set of errors as a timeout. The libraries may retry read
         * on these errors. */
        tlsStatus = 0;
    }
    else if( tlsStatus < 0 )
    {
        LogError( "Failed to read data: mbedTLSError= %s : %s.",
                    mbedtlsHighLevelCodeOrDefault( tlsStatus ),
                    mbedtlsLowLevelCodeOrDefault( tlsStatus ) );
    }
    else
    {
        /* Empty else marker. */
    }

    return tlsStatus;
}
/*-----------------------------------------------------------*/

int32_t mbedtls_transport_send( NetworkContext_t * pxNetworkContext,
                                const void * pBuffer,
                                size_t bytesToSend )
{
    TLSContext_t * pxTLSContext = ( TLSContext_t * ) pxNetworkContext;
    int32_t tlsStatus = 0;

    configASSERT( pxTLSContext != NULL );
    configASSERT( pxTLSContext->pxSocketInterface != NULL );
    configASSERT( pxTLSContext->pxSocketContext != NULL );

    tlsStatus = ( int32_t ) mbedtls_ssl_write( &( pxTLSContext->xSslContext ),
                                               pBuffer,
                                               bytesToSend );

    if( ( tlsStatus == MBEDTLS_ERR_SSL_TIMEOUT ) ||
        ( tlsStatus == MBEDTLS_ERR_SSL_WANT_READ ) ||
        ( tlsStatus == MBEDTLS_ERR_SSL_WANT_WRITE ) )
    {
        LogDebug( "Failed to send data. However, send can be retried on this error. "
                    "mbedTLSError= %s : %s.",
                    mbedtlsHighLevelCodeOrDefault( tlsStatus ),
                    mbedtlsLowLevelCodeOrDefault( tlsStatus ) );

        /* Mark these set of errors as a timeout. The libraries may retry send
         * on these errors. */
        tlsStatus = 0;
    }
    else if( tlsStatus < 0 )
    {
        LogError( "Failed to send data:  mbedTLSError= %s : %s.",
                    mbedtlsHighLevelCodeOrDefault( tlsStatus ),
                    mbedtlsLowLevelCodeOrDefault( tlsStatus ) );
    }
    else
    {
        /* Empty else marker. */
    }

    return tlsStatus;
}
/*-----------------------------------------------------------*/



#ifdef MBEDTLS_DEBUG_C
    static inline const char * pcMbedtlsLevelToFrLevel( int lLevel )
    {
        const char * pcFrLogLevel;
        switch( lLevel )
        {
        case 1:
            pcFrLogLevel = "ERR";
            break;
        case 2:
        case 3:
            pcFrLogLevel = "INF";
            break;
        case 4:
        default:
            pcFrLogLevel = "DBG";
            break;
        }
        return pcFrLogLevel;
    }

    /*-------------------------------------------------------*/

    static void vTLSDebugPrint( void *ctx,
                                int lLevel,
                                const char * pcFileName,
                                int lLineNumber,
                                const char * pcErrStr )
    {
        ( void ) ctx;

        vLoggingPrintf( pcMbedtlsLevelToFrLevel( lLevel ),
                        pcPathToBasename( pcFileName ),
                        lLineNumber,
                        pcErrStr );
    }
#endif


