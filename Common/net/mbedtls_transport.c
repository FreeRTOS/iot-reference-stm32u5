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

#define LOG_LEVEL    LOG_INFO

#include "logging.h"

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls_transport.h"
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"


/* mbedTLS includes. */
#include "mbedtls/error.h"
#include MBEDTLS_CONFIG_FILE
#include "mbedtls/debug.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/pem.h"
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/asn1.h"
#include "mbedtls/oid.h"
#include "pk_wrap.h"

#include "errno.h"

#define MBEDTLS_DEBUG_THRESHOLD    1

#ifdef MBEDTLS_TRANSPORT_PKCS11
#include "core_pkcs11_config.h"
#include "core_pkcs11.h"
#endif

typedef struct
{
    TaskHandle_t xTaskHandle;
    void * pvRecvReadyCallbackCtx;
    GenericCallback_t pxRecvReadyCallback;
    StackType_t puxStackBuffer[ 128 ];
    StaticTask_t xTaskBuffer;
    SockHandle_t xSockHandle;
} NotifyThreadCtx_t;

/**
 * @brief Secured connection context.
 */
typedef struct TLSContext
{
    ConnectionState_t xConnectionState;
    SockHandle_t xSockHandle;

    NotifyThreadCtx_t * pxNotifyThreadCtx;

    /* TLS connection */
    mbedtls_ssl_config xSslConfig;
    mbedtls_ssl_context xSslCtx;

    /* Certificates */
    mbedtls_x509_crt xRootCaChain;
    mbedtls_x509_crt xClientCert;

    /* Private Key */
    mbedtls_pk_context xPkCtx;

#ifdef MBEDTLS_TRANSPORT_PKCS11
    CK_SESSION_HANDLE xP11SessionHandle;
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

    /* Entropy Ctx */
    mbedtls_entropy_context xEntropyCtx;

#ifdef TRANSPORT_USE_CTR_DRBG
    mbedtls_ctr_drbg_context xCtrDrbgCtx;
#endif /* TRANSPORT_USE_CTR_DRBG */
} TLSContext_t;


/*-----------------------------------------------------------*/

static TlsTransportStatus_t xConfigureCertificateAuth( TLSContext_t * pxTLSCtx,
                                                       const PkiObject_t * pxPrivateKey,
                                                       const PkiObject_t * pxClientCert );

static TlsTransportStatus_t xConfigureCAChain( TLSContext_t * pxTLSCtx,
                                               const PkiObject_t * pxRootCaCerts,
                                               const size_t uxNumRootCA );

static inline void vStopSocketNotifyTask( NotifyThreadCtx_t * pxNotifyThreadCtx );

static void vCreateSocketNotifyTask( NotifyThreadCtx_t * pxNotifyThreadCtx,
                                     SockHandle_t xSockHandle );
static void vFreeNotifyThreadCtx( NotifyThreadCtx_t * pxNotifyThreadCtx );


static void vFreeNotifyThreadCtx( NotifyThreadCtx_t * pxNotifyThreadCtx );

#ifdef MBEDTLS_DEBUG_C
/* Used to print mbedTLS log output. */
static void vTLSDebugPrint( void * ctx,
                            int level,
                            const char * file,
                            int line,
                            const char * str );
#endif

/*-----------------------------------------------------------*/

static void vSocketNotifyThread( void * pvParameters )
{
    NotifyThreadCtx_t * pxCtx = ( NotifyThreadCtx_t * ) pvParameters;
    SockHandle_t xSockHandle = pxCtx->xSockHandle;
    BaseType_t xExitFlag = pdFALSE;
    fd_set xReadSet;
    fd_set xErrorSet;
    int lRslt;
    uint32_t ulNotifyValue = 0;

    ( void ) xTaskNotifyStateClear( NULL );

    if( pxCtx->pxRecvReadyCallback == NULL )
    {
        xExitFlag = pdTRUE;
    }

    while( !xExitFlag )
    {
        FD_ZERO( &xReadSet );
        FD_ZERO( &xErrorSet );
        FD_SET( xSockHandle, &xReadSet );
        FD_SET( xSockHandle, &xErrorSet );

        lRslt = sock_select( xSockHandle + 1, &xReadSet, NULL, &xErrorSet, NULL );

        xExitFlag |= ( lRslt == 0 );

        if( ( lRslt == 0 ) ||
            FD_ISSET( xSockHandle, &xErrorSet ) )
        {
            xExitFlag = pdTRUE;
        }
        else
        {
            if( FD_ISSET( xSockHandle, &xReadSet ) &&
                pxCtx->pxRecvReadyCallback )
            {
                pxCtx->pxRecvReadyCallback( pxCtx->pvRecvReadyCallbackCtx );
            }
        }

        if( xTaskNotifyWait( 0x0, 0xFFFFFFFF, &ulNotifyValue, portMAX_DELAY ) )
        {
            if( ulNotifyValue == 0xFFFFFFFF )
            {
                xExitFlag = pdTRUE;
            }
        }
    }

    /* Delete thread error */
    pxCtx->xTaskHandle = NULL;
    vTaskDelete( NULL );
}

/*-----------------------------------------------------------*/

static int32_t lMbedtlsErrToTransportError( int32_t lError )
{
    switch( lError )
    {
        case 0:
            lError = TLS_TRANSPORT_SUCCESS;
            break;

        case MBEDTLS_ERR_X509_ALLOC_FAILED:
            lError = TLS_TRANSPORT_INSUFFICIENT_MEMORY;
            break;

        case MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED:
            lError = TLS_TRANSPORT_INTERNAL_ERROR;
            break;

        case MBEDTLS_ERR_SSL_BAD_INPUT_DATA:
            lError = TLS_TRANSPORT_INVALID_PARAMETER;
            break;

        case MBEDTLS_ERR_X509_CERT_UNKNOWN_FORMAT:
        case MBEDTLS_ERR_X509_BAD_INPUT_DATA:
        case MBEDTLS_ERR_PEM_BAD_INPUT_DATA:
        case MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT:
            lError = TLS_TRANSPORT_PKI_OBJECT_PARSE_FAIL;
            break;

        default:
            lError = lError < 0 ? TLS_TRANSPORT_UNKNOWN_ERROR : TLS_TRANSPORT_SUCCESS;
            break;
    }

    return lError;
}

/*-----------------------------------------------------------*/
#ifndef MBEDTLS_X509_REMOVE_INFO
#ifdef X509_CRT_ERROR_INFO
#undef X509_CRT_ERROR_INFO
#endif /* X509_CRT_ERROR_INFO */
#define X509_CRT_ERROR_INFO( err, err_str, info ) \
    case err:                                     \
        pcVerifyInfo = info; break;
static const char * pcGetVerifyInfoString( unsigned int flag )
{
    const char * pcVerifyInfo = "Unknown Failure reason.";

    switch( flag )
    {
        MBEDTLS_X509_CRT_ERROR_INFO_LIST
        default:
            break;
    }

    return pcVerifyInfo;
}
#endif /* MBEDTLS_X509_REMOVE_INFO */

/*-----------------------------------------------------------*/


static void vLogCertificateVerifyResult( unsigned int flags )
{
#ifndef MBEDTLS_X509_REMOVE_INFO
    for( unsigned int mask = 1; mask != ( 1U << 31 ); mask = mask << 1 )
    {
        if( ( flags & mask ) > 0 )
        {
            LogError( "Certificate Verification Failure: %s", pcGetVerifyInfoString( flags & mask ) );
        }
    }
#endif /* !MBEDTLS_X509_REMOVE_INFO */
}

/*-----------------------------------------------------------*/

static size_t uxGetCertCNFromName( unsigned char ** ppucCommonName,
                                   mbedtls_x509_name * pxCertName )
{
    size_t uxCommonNameLen = 0;

    configASSERT( ppucCommonName != NULL );
    configASSERT( pxCertName != NULL );

    *ppucCommonName = NULL;

    for( ; pxCertName != NULL; pxCertName = pxCertName->next )
    {
        if( MBEDTLS_OID_CMP( MBEDTLS_OID_AT_CN, &( pxCertName->oid ) ) == 0 )
        {
            *ppucCommonName = pxCertName->MBEDTLS_PRIVATE( val ).p;
            uxCommonNameLen = pxCertName->MBEDTLS_PRIVATE( val ).len;
            break;
        }
    }

    return( uxCommonNameLen );
}

/*-----------------------------------------------------------*/

static void vLogCertInfo( mbedtls_x509_crt * pxCert,
                          const char * pcMessage )
{
    /* Iterate over added certs and print information */
    unsigned char * pucSubjectCN = NULL;
    unsigned char * pucIssuerCN = NULL;
    unsigned char * pucSerialNumber = NULL;
    char pcSerialNumberHex[ 41 ] = { 0 };
    size_t uxSubjectCNLen = 0;
    size_t uxIssuerCNLen = 0;
    size_t uxSerialNumberLen = 0;
    mbedtls_x509_time * pxValidFrom = NULL;
    mbedtls_x509_time * pxValidTo = NULL;

    uxSubjectCNLen = uxGetCertCNFromName( &pucSubjectCN,
                                          &( pxCert->subject ) );

    uxIssuerCNLen = uxGetCertCNFromName( &pucIssuerCN,
                                         &( pxCert->issuer ) );

    uxSerialNumberLen = pxCert->serial.len;
    pucSerialNumber = pxCert->serial.p;

    for( uint32_t i = 0; i < uxSerialNumberLen; i++ )
    {
        if( i == 21 )
        {
            break;
        }

        snprintf( &( pcSerialNumberHex[ i * 2 ] ), 3, "%.02X", pucSerialNumber[ i ] );
    }

    pxValidFrom = &( pxCert->valid_from );
    pxValidTo = &( pxCert->valid_to );

    if( pcMessage && pucSubjectCN && pucIssuerCN && pucSerialNumber && pxValidFrom && pxValidTo )
    {
        LogInfo( "%s CN=%.*s, SN:0x%s", pcMessage, uxSubjectCNLen, pucSubjectCN, pcSerialNumberHex );
        LogInfo( "Issuer: CN=%.*s", uxIssuerCNLen, pucIssuerCN );
        LogInfo( "Valid From: %04d-%02d-%02d, Expires: %04d-%02d-%02d",
                 pxValidFrom->year, pxValidFrom->mon, pxValidFrom->day,
                 pxValidTo->year, pxValidTo->mon, pxValidTo->day );
    }
}

/*-----------------------------------------------------------*/
/*TODO add proper timeout */
static int mbedtls_ssl_send( void * pvCtx,
                             const unsigned char * pcBuf,
                             size_t uxLen )
{
    SockHandle_t * pxSockHandle = ( SockHandle_t * ) pvCtx;
    int lError = 0;
    size_t uxBytesSent = 0;
    uint32_t ulBackofftimeMs = 1;

    if( ( pxSockHandle == NULL ) ||
        ( *pxSockHandle < 0 ) )
    {
        lError = MBEDTLS_ERR_NET_SOCKET_FAILED;
    }
    else
    {
        while( uxBytesSent < uxLen && lError == 0 )
        {
            ssize_t xRslt = sock_send( *pxSockHandle,
                                       ( void * const ) pcBuf,
                                       uxLen,
                                       0 );

            if( xRslt > 0 )
            {
                uxBytesSent += ( size_t ) xRslt;
            }
            else
            {
                lError = *__errno();

                if( lError != EWOULDBLOCK )
                {
                    LogError( "Got Error code: %ld", lError );
                }

                switch( lError )
                {
#if EAGAIN != EWOULDBLOCK
                    case EAGAIN:
#endif
                    case EINTR:
                    case EWOULDBLOCK:
                        break;

                    case EPIPE:
                    case ECONNRESET:
                        lError = MBEDTLS_ERR_NET_CONN_RESET;
                        break;

                    default:
                        lError = MBEDTLS_ERR_NET_SEND_FAILED;
                        break;
                }

                if( lError == EWOULDBLOCK )
                {
                    vTaskDelay( ulBackofftimeMs );
                    ulBackofftimeMs = ulBackofftimeMs * 2;
                    lError = 0;
                }
            }
        }
    }

    return ( int ) lError < 0 ? lError : uxBytesSent;
}

/*-----------------------------------------------------------*/

static int mbedtls_ssl_recv( void * pvCtx,
                             unsigned char * pcBuf,
                             size_t xLen )
{
    SockHandle_t * pxSockHandle = ( SockHandle_t * ) pvCtx;
    int lError = -1;

    if( ( pxSockHandle != NULL ) &&
        ( *pxSockHandle >= 0 ) )
    {
        lError = sock_recv( *pxSockHandle,
                            ( void * ) pcBuf,
                            xLen,
                            0 );
    }

    if( lError < 0 )
    {
        lError = *__errno();

        /* force use of newlibc errno */
        switch( *__errno() )
        {
#if EAGAIN != EWOULDBLOCK
            case EAGAIN:
#endif
            case EINTR:
            case EWOULDBLOCK:
                lError = MBEDTLS_ERR_SSL_WANT_READ;
                break;

            case EPIPE:
            case ECONNRESET:
                lError = MBEDTLS_ERR_NET_CONN_RESET;
                break;

            default:
                lError = MBEDTLS_ERR_NET_RECV_FAILED;
                break;
        }
    }

    return lError;
}

/*-----------------------------------------------------------*/

NetworkContext_t * mbedtls_transport_allocate( void )
{
    TLSContext_t * pxTLSCtx = NULL;

    pxTLSCtx = ( TLSContext_t * ) pvPortMalloc( sizeof( TLSContext_t ) );

    if( pxTLSCtx == NULL )
    {
        LogError( "Failed to allocate memory for TLSContext_t." );
    }
    else
    {
        pxTLSCtx->xConnectionState = STATE_ALLOCATED;
        pxTLSCtx->xSockHandle = -1;
        mbedtls_ssl_config_init( &( pxTLSCtx->xSslConfig ) );
        mbedtls_ssl_init( &( pxTLSCtx->xSslCtx ) );

        mbedtls_x509_crt_init( &( pxTLSCtx->xClientCert ) );
        mbedtls_x509_crt_init( &( pxTLSCtx->xRootCaChain ) );
        mbedtls_pk_init( &( pxTLSCtx->xPkCtx ) );

#ifdef MBEDTLS_TRANSPORT_PKCS11
        pxTLSCtx->xP11SessionHandle = CK_INVALID_HANDLE;
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

        mbedtls_entropy_init( &( pxTLSCtx->xEntropyCtx ) );

#ifdef TRANSPORT_USE_CTR_DRBG
        mbedtls_ctr_drbg_init( &( pxTLSCtx->xCtrDrbgCtx ) );
#endif /* TRANSPORT_USE_CTR_DRBG */

#ifdef MBEDTLS_THREADING_ALT
        mbedtls_platform_threading_init();
#endif /* MBEDTLS_THREADING_ALT */
    }

    return ( NetworkContext_t * ) pxTLSCtx;
}

/*-----------------------------------------------------------*/

void mbedtls_transport_free( NetworkContext_t * pxNetworkContext )
{
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;

    if( pxNetworkContext != NULL )
    {
        vFreeNotifyThreadCtx( pxTLSCtx->pxNotifyThreadCtx );

        if( pxTLSCtx->xSockHandle >= 0 )
        {
            ( void ) sock_close( pxTLSCtx->xSockHandle );
        }

        mbedtls_ssl_config_free( &( pxTLSCtx->xSslConfig ) );
        mbedtls_ssl_free( &( pxTLSCtx->xSslCtx ) );
        mbedtls_x509_crt_free( &( pxTLSCtx->xRootCaChain ) );
        mbedtls_x509_crt_free( &( pxTLSCtx->xClientCert ) );
        mbedtls_pk_free( &( pxTLSCtx->xPkCtx ) );

#ifdef MBEDTLS_TRANSPORT_PKCS11
        if( pxTLSCtx->xP11SessionHandle != CK_INVALID_HANDLE )
        {
            CK_FUNCTION_LIST_PTR pxFunctionList = NULL;

            if( ( C_GetFunctionList( &pxFunctionList ) == CKR_OK ) &&
                ( pxFunctionList->C_CloseSession != NULL ) )
            {
                pxFunctionList->C_CloseSession( pxTLSCtx->xP11SessionHandle );
            }
        }
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

#ifdef TRANSPORT_USE_CTR_DRBG
        mbedtls_entropy_free( &( pxTLSCtx->xEntropyCtx ) );
        mbedtls_ctr_drbg_free( &( pxTLSCtx->xCtrDrbgCtx ) );
#endif /* TRANSPORT_USE_CTR_DRBG */

        vPortFree( ( void * ) pxTLSCtx );
    }
}

/*-----------------------------------------------------------*/

static int lValidateCertByProfile( TLSContext_t * pxTLSCtx,
                                   mbedtls_x509_crt * pxCert )
{
    int lFlags = 0;
    const mbedtls_x509_crt_profile * pxCertProfile = NULL;

    if( ( pxTLSCtx == NULL ) || ( pxCert == NULL ) )
    {
        lFlags = -1;
    }
    else
    {
        pxCertProfile = pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( cert_profile );
    }

    if( pxCertProfile != NULL )
    {
        mbedtls_pk_context * pxPkCtx = &( pxCert->MBEDTLS_PRIVATE( pk ) );

        /* Check hashing algorithm */
        if( ( pxCertProfile->allowed_mds & MBEDTLS_X509_ID_FLAG( pxCert->MBEDTLS_PRIVATE( sig_md ) ) ) == 0 )
        {
            lFlags |= MBEDTLS_X509_BADCERT_BAD_MD;
        }

        if( ( pxCertProfile->allowed_pks & MBEDTLS_X509_ID_FLAG( pxCert->MBEDTLS_PRIVATE( sig_pk ) ) ) == 0 )
        {
            lFlags |= MBEDTLS_X509_BADCERT_BAD_PK;
        }

        /* Validate public key of cert */
#if defined( MBEDTLS_RSA_C )
        if( ( mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_RSA ) ||
            ( mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_RSASSA_PSS ) )
        {
            if( mbedtls_pk_get_bitlen( pxPkCtx ) < pxCertProfile->rsa_min_bitlen )
            {
                lFlags |= MBEDTLS_X509_BADCERT_BAD_KEY;
            }
        }
#endif /* MBEDTLS_RSA_C */

#if defined( MBEDTLS_ECP_C )
        if( ( mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_ECDSA ) ||
            ( mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_ECKEY ) ||
            ( mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_ECKEY_DH ) )
        {
            mbedtls_ecp_group_id xECGroupId = mbedtls_pk_ec( *pxPkCtx )->MBEDTLS_PRIVATE( grp ).id;

            if( ( pxCertProfile->allowed_curves & MBEDTLS_X509_ID_FLAG( xECGroupId ) ) == 0 )
            {
                lFlags |= MBEDTLS_X509_BADCERT_BAD_KEY;
            }
        }
#endif /* MBEDTLS_ECP_C */
    }

    return lFlags;
}

/*-----------------------------------------------------------*/

static TlsTransportStatus_t xConfigureCertificateAuth( TLSContext_t * pxTLSCtx,
                                                       const PkiObject_t * pxPrivateKey,
                                                       const PkiObject_t * pxClientCert )
{
    TlsTransportStatus_t xStatus = TLS_TRANSPORT_SUCCESS;
    mbedtls_pk_context * pxPkCtx = NULL;
    mbedtls_x509_crt * pxCertCtx = NULL;
    mbedtls_pk_context * pxCertPkCtx = NULL;

    configASSERT( pxTLSCtx );
    configASSERT( pxPrivateKey );
    configASSERT( pxClientCert );

    pxCertCtx = &( pxTLSCtx->xClientCert );
    pxPkCtx = &( pxTLSCtx->xPkCtx );

    /* Reset pk and certificate contexts if this is a reconfiguration */
    if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
    {
        mbedtls_pk_free( pxPkCtx );
        mbedtls_x509_crt_free( pxCertCtx );

        mbedtls_pk_init( pxPkCtx );
        mbedtls_x509_crt_init( pxCertCtx );
    }

    configASSERT( pxTLSCtx->xSslConfig.f_rng );

    xStatus = xPkiReadPrivateKey( pxPkCtx, pxPrivateKey,
                                  pxTLSCtx->xSslConfig.f_rng,
                                  pxTLSCtx->xSslConfig.p_rng );

    if( xStatus != TLS_TRANSPORT_SUCCESS )
    {
        LogError( "Failed to add private key to TLS context." );
    }
    else
    {
        xStatus = xPkiReadCertificate( pxCertCtx, pxClientCert );

        if( xStatus != TLS_TRANSPORT_SUCCESS )
        {
            LogError( "Failed to add client certificate to TLS context." );
        }
        else
        {
            pxCertPkCtx = &( pxCertCtx->MBEDTLS_PRIVATE( pk ) );
        }
    }

    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        int lRslt = lValidateCertByProfile( pxTLSCtx, pxCertCtx );

        if( lRslt != 0 )
        {
            vLogCertificateVerifyResult( lRslt );

            xStatus = TLS_TRANSPORT_CLIENT_CERT_INVALID;
        }
        else
        {
            vLogCertInfo( pxCertCtx, "Client Certificate:" );
        }
    }

    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        configASSERT( pxCertPkCtx );
        configASSERT( pxPkCtx );

        if( !( mbedtls_pk_can_do( pxCertPkCtx, MBEDTLS_PK_ECDSA ) &&
               mbedtls_pk_can_do( pxPkCtx, MBEDTLS_PK_ECDSA ) ) &&
            !( mbedtls_pk_can_do( pxCertPkCtx, MBEDTLS_PK_RSA ) &&
               mbedtls_pk_can_do( pxPkCtx, MBEDTLS_PK_RSA ) ) )
        {
            LogError( "Private key and client certificate have mismatched key types." );
            xStatus = TLS_TRANSPORT_INVALID_CREDENTIALS;
        }
    }

    /* Validate that the cert and pk match. */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        mbedtls_pk_context xTempPubKeyCtx;

        xTempPubKeyCtx.pk_ctx = pxCertPkCtx->pk_ctx;
        xTempPubKeyCtx.pk_info = pxPkCtx->pk_info;

        int lError = mbedtls_pk_check_pair( &xTempPubKeyCtx, pxPkCtx,
                                            pxTLSCtx->xSslConfig.f_rng,
                                            pxTLSCtx->xSslConfig.p_rng );

        MBEDTLS_MSG_IF_ERROR( lError, "Public-Private keypair does not match the provided certificate." );

        xStatus = ( lError == 0 ) ? TLS_TRANSPORT_SUCCESS : TLS_TRANSPORT_INVALID_CREDENTIALS;
    }

    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        int lError = mbedtls_ssl_conf_own_cert( &( pxTLSCtx->xSslConfig ),
                                                pxCertCtx, pxPkCtx );

        MBEDTLS_MSG_IF_ERROR( lError, "Failed to configure TLS client certificate " );

        xStatus = ( lError == 0 ) ? TLS_TRANSPORT_SUCCESS : TLS_TRANSPORT_INVALID_CREDENTIALS;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

static TlsTransportStatus_t xConfigureCAChain( TLSContext_t * pxTLSCtx,
                                               const PkiObject_t * pxRootCaCerts,
                                               const size_t uxNumRootCA )
{
    TlsTransportStatus_t xStatus = TLS_TRANSPORT_SUCCESS;

    mbedtls_x509_crt * pxRootCertIterator = NULL;
    mbedtls_x509_crt * pxRootCaChain = NULL;
    size_t uxValidCertCount = 0;
    int lError = 0;

    configASSERT( pxTLSCtx );
    configASSERT( pxRootCaCerts );
    configASSERT( uxNumRootCA );

    pxRootCaChain = &( pxTLSCtx->xRootCaChain );

    for( size_t uxIdx = 0; uxIdx < uxNumRootCA; uxIdx++ )
    {
        const PkiObject_t * pxRootCert = &( pxRootCaCerts[ uxIdx ] );
        mbedtls_x509_crt * pxTempCaCert = NULL;

        /* Heap allocate all but the first mbedtls_x509_crt object */
        if( pxRootCertIterator == NULL )
        {
            pxTempCaCert = pxRootCaChain;
        }
        else
        {
            pxTempCaCert = mbedtls_calloc( 1, sizeof( mbedtls_x509_crt ) );
        }

        /* If heap allocation failed, break out of loop */
        if( pxTempCaCert == NULL )
        {
            LogError( "Failed to allocate memory for mbedtls_x509_crt object." );
            lError = MBEDTLS_ERR_X509_ALLOC_FAILED;
        }
        else
        {
            mbedtls_x509_crt_init( pxTempCaCert );

            /* load the certificate onto the heap */
            lError = xPkiReadCertificate( pxTempCaCert, pxRootCert );

            MBEDTLS_LOG_IF_ERROR( lError, "Failed to load the CA Certificate at index: %ld, Error: ", uxIdx );
        }

        if( lError == 0 )
        {
            lError = lValidateCertByProfile( pxTLSCtx, pxTempCaCert );

            if( lError != 0 )
            {
#if !defined( MBEDTLS_X509_REMOVE_INFO )
                LogError( "Failed to validate the CA Certificate at index: %ld. Reason: %s", uxIdx,
                          pcGetVerifyInfoString( lError ) );
#else /* !defined( MBEDTLS_X509_REMOVE_INFO ) */
                LogError( "Failed to validate the CA Certificate at index: %ld.", uxIdx );
#endif
            }
        }

        if( lError == 0 )
        {
            vLogCertInfo( pxTempCaCert, "CA Certificate: " );

            /* Append to the list */
            if( pxRootCertIterator != NULL )
            {
                pxRootCertIterator->MBEDTLS_PRIVATE( next ) = pxTempCaCert;
            }

            pxRootCertIterator = pxTempCaCert;
            uxValidCertCount++;
        }
        /* Otherwise, handle the error */
        else if( pxTempCaCert != NULL )
        {
            /* Free any allocated data */
            mbedtls_x509_crt_free( pxTempCaCert );

            /* Free pxTempCaCert if it is heap allocated (not first in list) */
            if( pxRootCertIterator != NULL )
            {
                mbedtls_free( pxTempCaCert );
            }
        }

        /* Break on memory allocation failure */
        if( lError == MBEDTLS_ERR_X509_ALLOC_FAILED )
        {
            break;
        }
    }

    xStatus = lMbedtlsErrToTransportError( lError );

    if( ( uxValidCertCount == 0 ) &&
        ( lError != MBEDTLS_ERR_X509_ALLOC_FAILED ) )
    {
        LogError( "Failed to load any valid Root CA Certificates." );
        xStatus = TLS_TRANSPORT_NO_VALID_CA_CERT;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/
TlsTransportStatus_t mbedtls_transport_configure( NetworkContext_t * pxNetworkContext,
                                                  const char ** ppcAlpnProtos,
                                                  const PkiObject_t * pxPrivateKey,
                                                  const PkiObject_t * pxClientCert,
                                                  const PkiObject_t * pxRootCaCerts,
                                                  const size_t uxNumRootCA )
{
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;
    mbedtls_ssl_config * pxSslConfig = NULL;
    TlsTransportStatus_t xStatus = TLS_TRANSPORT_SUCCESS;
    int lError = 0;

    if( pxNetworkContext == NULL )
    {
        LogError( "Provided pxNetworkContext cannot be NULL." );
        xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else if( ( pxPrivateKey && !pxClientCert ) ||
             ( !pxPrivateKey && pxClientCert ) )
    {
        LogError( "pxPrivateKey and pxClientCert arguments are required for client certificate authentication." );
        xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else if( pxRootCaCerts == NULL )
    {
        LogError( "Provided pxRootCaCerts cannot be NULL." );
        xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else if( uxNumRootCA == 0 )
    {
        LogError( "Provided uxNumRootCA must be > 0." );
        xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else
    {
        pxSslConfig = &( pxTLSCtx->xSslConfig );
    }

    /* If already connected, disconnect */
    if( pxTLSCtx->xConnectionState == STATE_CONNECTED )
    {
        mbedtls_transport_disconnect( pxNetworkContext );
    }

    /* Setup new contexts */
    if( pxTLSCtx->xConnectionState == STATE_ALLOCATED )
    {
#ifdef MBEDTLS_TRANSPORT_PKCS11
        if( xStatus == TLS_TRANSPORT_SUCCESS )
        {
            if( xInitializePkcs11Session( &( pxTLSCtx->xP11SessionHandle ) ) != CKR_OK )
            {
                LogError( "Failed to initialize PKCS11 session." );

                xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
            }
        }
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

#ifdef MBEDTLS_TRANSPORT_PSA
        if( xStatus == TLS_TRANSPORT_SUCCESS )
        {
            if( psa_crypto_init() != PSA_SUCCESS )
            {
                LogError( "Failed to initialize PSA crypto interface." );

                xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
            }
        }
#endif /* MBEDTLS_TRANSPORT_PSA */
    }

    configASSERT( pxTLSCtx->xConnectionState != STATE_CONNECTED );

    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
#ifdef MBEDTLS_DEBUG_C
        mbedtls_ssl_conf_dbg( pxSslConfig, vTLSDebugPrint, NULL );
        mbedtls_debug_set_threshold( MBEDTLS_DEBUG_THRESHOLD );
#endif /* MBEDTLS_DEBUG_C */

        if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
        {
            mbedtls_ssl_config_free( pxSslConfig );
            mbedtls_ssl_config_init( pxSslConfig );
        }

        /* Initialize SSL Config from defaults */
        lError = mbedtls_ssl_config_defaults( pxSslConfig,
                                              MBEDTLS_SSL_IS_CLIENT,
                                              MBEDTLS_SSL_TRANSPORT_STREAM,
                                              MBEDTLS_SSL_PRESET_DEFAULT );

        MBEDTLS_MSG_IF_ERROR( lError, "Failed to initialize ssl configuration: Error:" );

        xStatus = lMbedtlsErrToTransportError( lError );
    }

    /* Setup entropy / rng contexts */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        int lRslt = 0;

#ifdef TRANSPORT_USE_CTR_DRBG
        /* Seed the local RNG. */
        lRslt = mbedtls_ctr_drbg_seed( &( pxTLSCtx->xCtrDrbgCtx ),
                                       mbedtls_entropy_func,
                                       &( pxTLSCtx->xEntropyCtx ),
                                       NULL,
                                       0 );

        if( lRslt < 0 )
        {
            LogError( "Failed to seed PRNG: Error: %s : %s.",
                      mbedtlsHighLevelCodeOrDefault( lRslt ),
                      mbedtlsLowLevelCodeOrDefault( lRslt ) );
            xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
        }
        else
        {
            mbedtls_ssl_conf_rng( &( pxTLSCtx->xSslConfig ),
                                  mbedtls_ctr_drbg_random,
                                  &( pxTLSCtx->xCtrDrbgCtx ) );
        }
#elif defined( MBEDTLS_TRANSPORT_PSA )
        mbedtls_ssl_conf_rng( &( pxTLSCtx->xSslConfig ),
                              lPSARandomCallback,
                              NULL );
#else /* ifdef TRANSPORT_USE_CTR_DRBG */
        mbedtls_ssl_conf_rng( &( pxTLSCtx->xSslConfig ),
                              mbedtls_entropy_func,
                              &( pxTLSCtx->xEntropyCtx ) );
#endif /* ifdef TRANSPORT_USE_CTR_DRBG */


        xStatus = ( lError == 0 ) ? TLS_TRANSPORT_SUCCESS : TLS_TRANSPORT_INTERNAL_ERROR;
    }

    /* Configure security level settings */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        /* Set minimum ssl / tls version */
        mbedtls_ssl_conf_min_version( pxSslConfig,
                                      MBEDTLS_SSL_MAJOR_VERSION_3,
                                      MBEDTLS_SSL_MINOR_VERSION_3 );

        mbedtls_ssl_conf_cert_profile( pxSslConfig, &mbedtls_x509_crt_profile_default );

        mbedtls_ssl_conf_authmode( pxSslConfig, MBEDTLS_SSL_VERIFY_REQUIRED );
    }

    /* Configure certificate auth if a cert and key were provided */
    if( ( xStatus == TLS_TRANSPORT_SUCCESS ) &&
        pxPrivateKey && pxClientCert )
    {
        xStatus = xConfigureCertificateAuth( pxTLSCtx, pxPrivateKey, pxClientCert );
    }

    /* Configure ALPN Protocols */
    if( ( xStatus == TLS_TRANSPORT_SUCCESS ) &&
        ppcAlpnProtos )
    {
        /* Include an application protocol list in the TLS ClientHello
         * message. */
        lError = mbedtls_ssl_conf_alpn_protocols( pxSslConfig, ppcAlpnProtos );

        MBEDTLS_MSG_IF_ERROR( lError, "Failed to configure ALPN protocols: " );

        xStatus = lMbedtlsErrToTransportError( lError );
    }

    /* Set Maximum Fragment Length if enabled. */
#ifdef MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        /* Enable the max fragment extension. 4096 bytes is currently the largest fragment size permitted.
         * See RFC 8449 https://tools.ietf.org/html/rfc8449 for more information.
         *
         * Smaller values can be found in "mbedtls/include/ssl.h".
         */
        lError = mbedtls_ssl_conf_max_frag_len( pxSslConfig, MBEDTLS_SSL_MAX_FRAG_LEN_4096 );

        MBEDTLS_MSG_IF_ERROR( lError, "Failed to configure maximum fragment length extension, " );
        xStatus = lMbedtlsErrToTransportError( lError );
    }
#endif /* MBEDTLS_SSL_MAX_FRAGMENT_LENGTH */

    /* Load CA certificate chain. */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
        {
            mbedtls_x509_crt_free( &( pxTLSCtx->xRootCaChain ) );
            mbedtls_x509_crt_init( &( pxTLSCtx->xRootCaChain ) );
        }

        xStatus = xConfigureCAChain( pxTLSCtx, pxRootCaCerts, uxNumRootCA );

        mbedtls_ssl_conf_ca_chain( pxSslConfig, &( pxTLSCtx->xRootCaChain ), NULL );
    }

    /* Initialize SSL context */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        mbedtls_ssl_context * pxSslCtx = &( pxTLSCtx->xSslCtx );

        /* Clear the ssl connection context if we're reconfiguring */
        if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
        {
            mbedtls_ssl_free( pxSslCtx );
            mbedtls_ssl_init( pxSslCtx );
        }

        /* Setup tls connection context and associate it with the tls config. */
        lError = mbedtls_ssl_setup( pxSslCtx, pxSslConfig );

        MBEDTLS_MSG_IF_ERROR( lError, "Call to mbedtls_ssl_setup failed, " );

        if( lError != 0 )
        {
            xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
        }
        else
        {
            /* Setup mbedtls IO callbacks */
            mbedtls_ssl_set_bio( pxSslCtx, &( pxTLSCtx->xSockHandle ),
                                 mbedtls_ssl_send, mbedtls_ssl_recv, NULL );

            pxTLSCtx->xConnectionState = STATE_CONFIGURED;
        }
    }

    return xStatus;
}

static TlsTransportStatus_t xConnectSocket( TLSContext_t * pxTLSCtx,
                                            const char * pcHostName,
                                            uint16_t usPort )
{
    TlsTransportStatus_t xStatus = TLS_TRANSPORT_SUCCESS;
    int lError = 0;
    struct addrinfo * pxAddrInfo = NULL;

    configASSERT( pxTLSCtx != NULL );
    configASSERT( pcHostName != NULL );
    configASSERT( usPort > 0 );

    /* Close socket if already allocated */
    if( pxTLSCtx->xSockHandle >= 0 )
    {
        ( void ) sock_close( pxTLSCtx->xSockHandle );
        pxTLSCtx->xSockHandle = -1;
    }

    /* Perform address (DNS) lookup */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        const struct addrinfo xAddrInfoHint =
        {
            .ai_family   = AF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP,
        };

        lError = dns_getaddrinfo( pcHostName, NULL,
                                  &xAddrInfoHint, &pxAddrInfo );

        if( ( lError != 0 ) || ( pxAddrInfo == NULL ) )
        {
            LogError( "Failed to resolve hostname: %s to IP address.", pcHostName );
            xStatus = TLS_TRANSPORT_DNS_FAILED;

            if( pxAddrInfo != NULL )
            {
                dns_freeaddrinfo( pxAddrInfo );
                pxAddrInfo = NULL;
            }
        }
    }

    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        struct addrinfo * pxAddrIter = NULL;

        /* Try all of the addresses returned by getaddrinfo */
        for( pxAddrIter = pxAddrInfo; pxAddrIter != NULL; pxAddrIter = pxAddrIter->ai_next )
        {
            /* Set port number */
            switch( pxAddrIter->ai_family )
            {
#if LWIP_IPV4 == 1
                case AF_INET:
                    ( ( struct sockaddr_in * ) pxAddrIter->ai_addr )->sin_port = htons( usPort );
                    break;
#endif
#if LWIP_IPV6 == 1
                case AF_INET6:
                    ( ( struct sockaddr_in6 * ) pxAddrIter->ai_addr )->sin6_port = htons( usPort );
                    break;
#endif
                default:
                    continue;
                    break;
            }

#if LWIP_IPV4 == 1
            if( pxAddrIter->ai_family == AF_INET )
            {
                char ipAddrBuff[ IP4ADDR_STRLEN_MAX ] = { 0 };
                ( void ) inet_ntoa_r( ( ( struct sockaddr_in * ) pxAddrIter->ai_addr )->sin_addr, ipAddrBuff, IP4ADDR_STRLEN_MAX );
                LogInfo( "Trying address: %.*s, port: %uh for host: %s.",
                         IP4ADDR_STRLEN_MAX, ipAddrBuff, usPort, pcHostName );
            }
#endif
#if LWIP_IPV6 == 1
            if( pxAddrIter->ai_family == AF_INET6 )
            {
                char ipAddrBuff[ IP6ADDR_STRLEN_MAX ] = { 0 };
                ( void ) inet6_ntoa_r( ( ( struct sockaddr_in6 * ) pxAddrIter->ai_addr )->sin_addr, ipAddrBuff, IP6ADDR_STRLEN_MAX );
                LogInfo( "Trying address: %.*s, port: %uh for host: %s.",
                         IP6ADDR_STRLEN_MAX, ipAddrBuff, usPort, pcHostName );
            }
#endif

            /* Allocate socket */
            pxTLSCtx->xSockHandle = sock_socket( pxAddrIter->ai_family,
                                                 pxAddrIter->ai_socktype,
                                                 pxAddrIter->ai_protocol );

            if( pxTLSCtx->xSockHandle < 0 )
            {
                LogError( "Failed to allocate socket." );
                xStatus = TLS_TRANSPORT_INSUFFICIENT_SOCKETS;
            }
            else
            {
                lError = sock_connect( pxTLSCtx->xSockHandle,
                                       pxAddrIter->ai_addr,
                                       pxAddrIter->ai_addrlen );

                /* Upon connection error, continue to next address */
                if( lError != 0 )
                {
                    ( void ) sock_close( pxTLSCtx->xSockHandle );
                    pxTLSCtx->xSockHandle = -1;
                }
                else
                {
#if LWIP_IPV4 == 1
                    if( pxAddrIter->ai_family == AF_INET )
                    {
                        char ipAddrBuff[ IP4ADDR_STRLEN_MAX ] = { 0 };

                        ( void ) inet_ntoa_r( ( ( struct sockaddr_in * ) pxAddrIter->ai_addr )->sin_addr, ipAddrBuff, IP4ADDR_STRLEN_MAX );

                        LogInfo( "Connected socket: %ld to host: %s, address: %.*s, port: %uh.",
                                 pxTLSCtx->xSockHandle, pcHostName,
                                 IP4ADDR_STRLEN_MAX, ipAddrBuff, usPort );
                    }
#endif /* if LWIP_IPV4 == 1 */
#if LWIP_IPV6 == 1
                    if( pxAddrIter->ai_family == AF_INET6 )
                    {
                        char ipAddrBuff[ IP6ADDR_STRLEN_MAX ] = { 0 };
                        ( void ) inet6_ntoa_r( ( ( struct sockaddr_in6 * ) pxAddrIter->ai_addr )->sin_addr, ipAddrBuff, IP6ADDR_STRLEN_MAX );
                        LogInfo( "Connected socket: %ld to host: %s, address: %.*s, port: %uh.",
                                 pxTLSCtx->xSockHandle, pcHostName,
                                 IP6ADDR_STRLEN_MAX, ipAddrBuff, usPort );
                    }
#endif
                }
            }

            /* Exit loop on an irrecoverable error or successful connection. */
            if( ( xStatus != TLS_TRANSPORT_SUCCESS ) ||
                ( pxTLSCtx->xSockHandle >= 0 ) )
            {
                break;
            }
        }
    }

    if( pxAddrInfo != NULL )
    {
        dns_freeaddrinfo( pxAddrInfo );
        pxAddrInfo = NULL;
    }

    if( ( xStatus == TLS_TRANSPORT_SUCCESS ) &&
        ( pxTLSCtx->xSockHandle < 0 ) )
    {
        xStatus = TLS_TRANSPORT_CONNECT_FAILURE;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

TlsTransportStatus_t mbedtls_transport_connect( NetworkContext_t * pxNetworkContext,
                                                const char * pcHostName,
                                                uint16_t usPort,
                                                uint32_t ulRecvTimeoutMs,
                                                uint32_t ulSendTimeoutMs )
{
    TlsTransportStatus_t xStatus = TLS_TRANSPORT_SUCCESS;
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;
    mbedtls_ssl_context * pxSslCtx = NULL;
    int lError = 0;

    configASSERT( pxTLSCtx != NULL );

    if( pxNetworkContext == NULL )
    {
        LogError( "Invalid input parameter: Arguments cannot be NULL. pxNetworkContext=%p.",
                  pxNetworkContext );
        xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else if( pcHostName == NULL )
    {
        LogError( "Provided pcHostName cannot be NULL." );
        xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else if( strnlen( pcHostName, MBEDTLS_SSL_MAX_HOST_NAME_LEN + 1 ) > MBEDTLS_SSL_MAX_HOST_NAME_LEN )
    {
        LogError( "Provided pcHostName parameter must not exceed %ld characters.", MBEDTLS_SSL_MAX_HOST_NAME_LEN );
        xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else if( usPort == 0 )
    {
        LogError( "Provided usPort parameter must not be 0." );
        xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
    }
    else
    {
        pxSslCtx = &( pxTLSCtx->xSslCtx );
    }

    /* Set hostname for SNI and server certificate verification */
    if( ( xStatus == TLS_TRANSPORT_SUCCESS ) &&
        ( ( pxTLSCtx->xSslCtx.MBEDTLS_PRIVATE( hostname ) == NULL ) ||
          ( strncmp( pxTLSCtx->xSslCtx.MBEDTLS_PRIVATE( hostname ), pcHostName, MBEDTLS_SSL_MAX_HOST_NAME_LEN ) != 0 ) ) )
    {
        lError = mbedtls_ssl_set_hostname( pxSslCtx, pcHostName );

        if( lError != 0 )
        {
            LogError( "Failed to set server hostname: Error: %s : %s.",
                      mbedtlsHighLevelCodeOrDefault( lError ),
                      mbedtlsLowLevelCodeOrDefault( lError ) );
            xStatus = TLS_TRANSPORT_INVALID_HOSTNAME;
        }
    }

    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        xStatus = xConnectSocket( pxTLSCtx, pcHostName, usPort );
    }

    /* Set send and receive timeout parameters */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        lError = sock_setsockopt( pxTLSCtx->xSockHandle,
                                  SOL_SOCKET,
                                  SO_RCVTIMEO,
                                  ( void * ) &ulRecvTimeoutMs,
                                  sizeof( ulRecvTimeoutMs ) );

        if( lError != SOCK_OK )
        {
            LogError( "Failed to set SO_RCVTIMEO socket option." );
            xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
        }
    }

    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        lError |= sock_setsockopt( pxTLSCtx->xSockHandle,
                                   SOL_SOCKET,
                                   SO_SNDTIMEO,
                                   ( void * ) &ulSendTimeoutMs,
                                   sizeof( ulSendTimeoutMs ) );

        if( lError != SOCK_OK )
        {
            LogError( "Failed to set SO_SNDTIMEO socket option." );
            xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
        }
    }

    if( ( xStatus == TLS_TRANSPORT_SUCCESS ) &&
        ( ulRecvTimeoutMs == 0 ) )
    {
        int flags = sock_fcntl( pxTLSCtx->xSockHandle, F_GETFL, 0 );

        if( flags == -1 )
        {
            xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
            LogError( "Failed to get socket flags." );
        }
        else
        {
            flags = ( flags | O_NONBLOCK );

            if( sock_fcntl( pxTLSCtx->xSockHandle, F_SETFL, flags ) != 0 )
            {
                xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
                LogError( "Failed to set socket O_NONBLOCK flag." );
            }
        }
    }

    /* Perform TLS handshake. */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        /* Perform the TLS handshake. */
        do
        {
            lError = mbedtls_ssl_handshake( pxSslCtx );
        }
        while( ( lError == MBEDTLS_ERR_SSL_WANT_READ ) ||
               ( lError == MBEDTLS_ERR_SSL_WANT_WRITE ) );

        if( lError != 0 )
        {
            LogError( "Failed to perform TLS handshake: Error: %s : %s.",
                      mbedtlsHighLevelCodeOrDefault( lError ),
                      mbedtlsLowLevelCodeOrDefault( lError ) );

            xStatus = TLS_TRANSPORT_HANDSHAKE_FAILED;
        }
        else
        {
            LogInfo( "Network connection %p: TLS handshake successful.",
                     pxTLSCtx );
        }
    }

    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        LogInfo( "Network connection %p: Connection to %s:%u established.",
                 pxNetworkContext, pcHostName, usPort );

        if( ( xStatus == TLS_TRANSPORT_SUCCESS ) &&
            pxTLSCtx->pxNotifyThreadCtx )
        {
            vCreateSocketNotifyTask( pxTLSCtx->pxNotifyThreadCtx, pxTLSCtx->xSockHandle );
        }

        pxTLSCtx->xConnectionState = STATE_CONNECTED;
    }
    else
    {
        /* Clean up on failure. */
        if( ( pxNetworkContext != NULL ) &&
            ( pxTLSCtx->xSockHandle >= 0 ) )
        {
            /* Deallocate the open socket. */
            sock_close( pxTLSCtx->xSockHandle );
            pxTLSCtx->xSockHandle = -1;
        }

        /* Reset SSL session context for reconnect attempt */
        mbedtls_ssl_session_reset( pxSslCtx );

        LogInfo( "Network connection %p: to %s:%u failed.",
                 pxNetworkContext,
                 pcHostName, usPort );
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

static inline void vStopSocketNotifyTask( NotifyThreadCtx_t * pxNotifyThreadCtx )
{
    configASSERT( pxNotifyThreadCtx );

    while( ( pxNotifyThreadCtx->xTaskHandle != NULL ) &&
           ( eTaskGetState( pxNotifyThreadCtx->xTaskHandle ) != eDeleted ) )
    {
        ( void ) xTaskNotify( pxNotifyThreadCtx->xTaskHandle, 0xFFFFFFFF, eSetValueWithOverwrite );
        vTaskDelay( 1 );
    }
}

/*-----------------------------------------------------------*/

static void vCreateSocketNotifyTask( NotifyThreadCtx_t * pxNotifyThreadCtx,
                                     SockHandle_t xSockHandle )
{
    TaskHandle_t xTaskHandle = NULL;

    configASSERT_CONTINUE( pxNotifyThreadCtx );

    vStopSocketNotifyTask( pxNotifyThreadCtx );

    if( pxNotifyThreadCtx &&
        pxNotifyThreadCtx->pxRecvReadyCallback )
    {
        pxNotifyThreadCtx->xSockHandle = xSockHandle;

        xTaskHandle = xTaskCreateStatic( vSocketNotifyThread,
                                         "SockNotify",
                                         128,
                                         ( void * ) pxNotifyThreadCtx,
                                         tskIDLE_PRIORITY,
                                         pxNotifyThreadCtx->puxStackBuffer,
                                         &( pxNotifyThreadCtx->xTaskBuffer ) );

        pxNotifyThreadCtx->xTaskHandle = xTaskHandle;
    }
}
/*-----------------------------------------------------------*/

static void vFreeNotifyThreadCtx( NotifyThreadCtx_t * pxNotifyThreadCtx )
{
    if( pxNotifyThreadCtx )
    {
        vStopSocketNotifyTask( pxNotifyThreadCtx );

        vPortFree( pxNotifyThreadCtx );
    }
}

/*-----------------------------------------------------------*/

int32_t mbedtls_transport_setrecvcallback( NetworkContext_t * pxNetworkContext,
                                           GenericCallback_t pxCallback,
                                           void * pvCtx )
{
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;
    NotifyThreadCtx_t * pxNotifyThreadCtx = NULL;
    int32_t lError = 0;

    if( ( pxTLSCtx == NULL ) ||
        ( pxCallback == NULL ) )
    {
        lError = -1;
    }
    else
    {
        pxNotifyThreadCtx = pxTLSCtx->pxNotifyThreadCtx;
    }

    if( pxNotifyThreadCtx == NULL )
    {
        pxNotifyThreadCtx = pvPortMalloc( sizeof( NotifyThreadCtx_t ) );

        if( pxNotifyThreadCtx == NULL )
        {
            LogError( "Failed to allocate memory for a NotifyThreadCtx_t." );
            lError = -1;
        }
        else
        {
            pxTLSCtx->pxNotifyThreadCtx = pxNotifyThreadCtx;
            pxNotifyThreadCtx->xSockHandle = -1;
            pxNotifyThreadCtx->xTaskHandle = 0;
        }
    }
    /* Connected and pxNotifyThreadCtx already exists */
    else if( ( pxTLSCtx->xConnectionState == STATE_CONNECTED ) &&
             pxNotifyThreadCtx->xTaskHandle )
    {
        vStopSocketNotifyTask( pxNotifyThreadCtx );
    }
    else
    {
        /* Empty */
    }

    if( lError == 0 )
    {
        pxNotifyThreadCtx->pxRecvReadyCallback = pxCallback;
        pxNotifyThreadCtx->pvRecvReadyCallbackCtx = pvCtx;

        if( pxTLSCtx->xConnectionState == STATE_CONNECTED )
        {
            vCreateSocketNotifyTask( pxNotifyThreadCtx, pxTLSCtx->xSockHandle );
        }
    }

    return lError;
}

/*-----------------------------------------------------------*/

int32_t mbedtls_transport_setsockopt( NetworkContext_t * pxNetworkContext,
                                      int32_t lSockopt,
                                      const void * pvSockoptValue,
                                      uint32_t ulOptionLen )
{
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;
    int32_t sockError = -EINVAL;

    configASSERT( pxTLSCtx != NULL );
    configASSERT( pvSockoptValue != NULL );

    if( ( pxTLSCtx != NULL ) &&
        ( pxTLSCtx->xSockHandle >= 0 ) )
    {
        sockError = sock_setsockopt( pxTLSCtx->xSockHandle,
                                     SOL_SOCKET,
                                     lSockopt,
                                     pvSockoptValue,
                                     ulOptionLen );
    }

    return sockError;
}

/*-----------------------------------------------------------*/
void mbedtls_transport_disconnect( NetworkContext_t * pxNetworkContext )
{
    BaseType_t tlsStatus = 0;
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;

    configASSERT( pxNetworkContext != NULL );

    if( pxNetworkContext != NULL )
    {
        if( pxTLSCtx->xConnectionState == STATE_CONNECTED )
        {
            /* Notify the server to close */
            tlsStatus = ( BaseType_t ) mbedtls_ssl_close_notify( &( pxTLSCtx->xSslCtx ) );

            /* Ignore the WANT_READ and WANT_WRITE return values. */
            if( ( tlsStatus != ( BaseType_t ) MBEDTLS_ERR_SSL_WANT_READ ) &&
                ( tlsStatus != ( BaseType_t ) MBEDTLS_ERR_SSL_WANT_WRITE ) )
            {
                if( tlsStatus == 0 )
                {
                    LogInfo( "Network connection %p: TLS close-notify sent.",
                             pxNetworkContext );
                }
                else
                {
                    LogError( "Network connection %p: Failed to send TLS close-notify: Error: %s : %s.",
                              pxNetworkContext,
                              mbedtlsHighLevelCodeOrDefault( tlsStatus ),
                              mbedtlsLowLevelCodeOrDefault( tlsStatus ) );
                }
            }

            pxTLSCtx->xConnectionState = STATE_CONFIGURED;
        }

        if( pxTLSCtx->pxNotifyThreadCtx )
        {
            vStopSocketNotifyTask( pxTLSCtx->pxNotifyThreadCtx );
        }

        if( pxTLSCtx->xSockHandle >= 0 )
        {
            /* Call socket close function to deallocate the socket. */
            sock_close( pxTLSCtx->xSockHandle );
            pxTLSCtx->xSockHandle = -1;
        }

        /* Clear SSL connection context for re-use */
        if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
        {
            mbedtls_ssl_session_reset( &( pxTLSCtx->xSslCtx ) );
        }
    }
}

/*-----------------------------------------------------------*/

int32_t mbedtls_transport_recv( NetworkContext_t * pxNetworkContext,
                                void * pBuffer,
                                size_t uxBytesToRecv )
{
    int32_t tlsStatus = 0;
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;

    configASSERT( pxNetworkContext != NULL );
    configASSERT( pBuffer != NULL );
    configASSERT( uxBytesToRecv > 0 );

    if( pxTLSCtx->xConnectionState == STATE_CONNECTED )
    {
        tlsStatus = ( int32_t ) mbedtls_ssl_read( &( pxTLSCtx->xSslCtx ),
                                                  pBuffer,
                                                  uxBytesToRecv );
    }
    else
    {
        tlsStatus = 0;
    }

    if( ( tlsStatus == MBEDTLS_ERR_SSL_TIMEOUT ) ||
        ( tlsStatus == MBEDTLS_ERR_SSL_WANT_READ ) ||
        ( tlsStatus == MBEDTLS_ERR_SSL_WANT_WRITE ) )
    {
        /* Mark these set of errors as a timeout. The libraries may retry read
         * on these errors. */
        tlsStatus = 0;
    }
    /* Close the Socket if needed. */
    else if( ( tlsStatus == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ) ||
             ( tlsStatus == MBEDTLS_ERR_NET_CONN_RESET ) )
    {
        tlsStatus = -1;
        pxTLSCtx->xConnectionState = STATE_CONFIGURED;

        if( pxTLSCtx->xSockHandle >= 0 )
        {
            if( pxTLSCtx->pxNotifyThreadCtx )
            {
                vStopSocketNotifyTask( pxTLSCtx->pxNotifyThreadCtx );
            }

            sock_close( pxTLSCtx->xSockHandle );
            pxTLSCtx->xSockHandle = -1;
        }
    }
    else if( tlsStatus < 0 )
    {
        LogError( "Failed to read data: Error: %s : %s.",
                  mbedtlsHighLevelCodeOrDefault( tlsStatus ),
                  mbedtlsLowLevelCodeOrDefault( tlsStatus ) );
    }
    else
    {
        if( pxTLSCtx->pxNotifyThreadCtx &&
            pxTLSCtx->pxNotifyThreadCtx->xTaskHandle )
        {
            ( void ) xTaskNotify( pxTLSCtx->pxNotifyThreadCtx->xTaskHandle, 0x0, eSetValueWithOverwrite );
        }
    }

    return tlsStatus;
}
/*-----------------------------------------------------------*/

int32_t mbedtls_transport_send( NetworkContext_t * pxNetworkContext,
                                const void * pBuffer,
                                size_t uxBytesToSend )
{
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;
    int32_t tlsStatus = 0;

    configASSERT( pxTLSCtx != NULL );
    configASSERT( pBuffer != NULL );
    configASSERT( uxBytesToSend > 0 );

    if( pxTLSCtx->xConnectionState == STATE_CONNECTED )
    {
        tlsStatus = ( int32_t ) mbedtls_ssl_write( &( pxTLSCtx->xSslCtx ),
                                                   pBuffer,
                                                   uxBytesToSend );
    }
    else
    {
        tlsStatus = 0;
    }

    if( ( tlsStatus == MBEDTLS_ERR_SSL_TIMEOUT ) ||
        ( tlsStatus == MBEDTLS_ERR_SSL_WANT_READ ) ||
        ( tlsStatus == MBEDTLS_ERR_SSL_WANT_WRITE ) )
    {
        /* Mark these set of errors as a timeout. The libraries may retry send
         * on these errors. */
        tlsStatus = 0;
    }
    /* Close the Socket if needed. */
    else if( ( tlsStatus == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ) ||
             ( tlsStatus == MBEDTLS_ERR_NET_CONN_RESET ) )
    {
        tlsStatus = -1;
        pxTLSCtx->xConnectionState = STATE_CONFIGURED;

        if( pxTLSCtx->xSockHandle >= 0 )
        {
            if( pxTLSCtx->pxNotifyThreadCtx )
            {
                vStopSocketNotifyTask( pxTLSCtx->pxNotifyThreadCtx );
            }

            sock_close( pxTLSCtx->xSockHandle );
            pxTLSCtx->xSockHandle = -1;
        }
    }
    else if( tlsStatus < 0 )
    {
        LogError( "Failed to send data:  Error: %s : %s.",
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

static inline const char * pcPathToBasename( const char * pcFileName )
{
    const char * pcIter = pcFileName;
    const char * pcBasename = pcFileName;

    /* Extract basename from file name */
    while( *pcIter != '\0' )
    {
        if( ( *pcIter == '/' ) || ( *pcIter == '\\' ) )
        {
            pcBasename = pcIter + 1;
        }

        pcIter++;
    }

    return pcBasename;
}

/*-------------------------------------------------------*/

static void vTLSDebugPrint( void * ctx,
                            int lLevel,
                            const char * pcFileName,
                            int lLineNumber,
                            const char * pcErrStr )
{
    const char * pcLogLevel;
    const char * pcFileBaseName;

    ( void ) ctx;

    pcLogLevel = pcMbedtlsLevelToFrLevel( lLevel );
    pcFileBaseName = pcPathToBasename( pcFileName );

    vLoggingPrintf( pcLogLevel, pcFileBaseName, lLineNumber, pcErrStr );
}
#endif /* ifdef MBEDTLS_DEBUG_C */
