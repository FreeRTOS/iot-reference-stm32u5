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
#include "mbedtls_config_ntz.h"
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

#define MBEDTLS_DEBUG_THRESHOLD 1

#ifdef MBEDTLS_TRANSPORT_PKCS11
#include "core_pkcs11_config.h"
#include "core_pkcs11.h"
#endif

/**
 * @brief Secured connection context.
 */
typedef struct TLSContext
{
	ConnectionState_t xConnectionState;
	SockHandle_t xSockHandle;

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

#ifdef TRANSPORT_USE_CTR_DRBG
    /* Entropy Ctx */
    mbedtls_entropy_context xEntropyCtx;
	mbedtls_ctr_drbg_context xCtrDrbgCtx;
#endif /* TRANSPORT_USE_CTR_DRBG */
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
* @brief Add X509 certificate to a given mbedtls_x509_crt list.
* @param[out] pxMbedtlsCertCtx Pointer to an mbedtls X509 certificate chain object.
* @param[in] pxCertificate Pointer to a PkiObject_t describing a certificate to be loaded.
* @param[in] pxTLSCtx Pointer to the TLS transport context.
*
* @return 0 on success; otherwise, failure;
*/
static int32_t lAddCertificate( mbedtls_x509_crt * pxMbedtlsCertCtx,
								const PkiObject_t * pxCertificate,
								const TLSContext_t * pxTLSCtx );

/**
 * @brief Initialize the private key object
 *
 * @param[out] pxPkCtx Mbedtls pk_context object to map the key to.
 * @param[in] pxTLSCtx SSL context to which the private key is to be set.
 * @param[in] pxPrivateKey PkiObject_t representing the key to load.
 *
 * @return 0 on success; otherwise, failure;
 */
static int32_t lLoadPrivateKey( mbedtls_pk_context * pxPkCtx,
								TLSContext_t * pxTLSCtx,
                                const PkiObject_t * pxPrivateKey );

#ifdef MBEDTLS_DEBUG_C
/* Used to print mbedTLS log output. */
static void vTLSDebugPrint( void *ctx, int level, const char *file, int line, const char *str );
#endif

/*-----------------------------------------------------------*/

static int32_t lP11ErrToTransportError( CK_RV xError )
{
	int32_t lError;
	switch( xError )
	{
	case CKR_OBJECT_HANDLE_INVALID:
		lError = TLS_TRANSPORT_PKI_OBJECT_NOT_FOUND;
		break;
	case CKR_HOST_MEMORY:
		lError = TLS_TRANSPORT_INSUFFICIENT_MEMORY;
		break;
	case CKR_FUNCTION_FAILED:
		lError = TLS_TRANSPORT_INTERNAL_ERROR;
		break;
	default:
		lError = TLS_TRANSPORT_UNKNOWN_ERROR;
		break;
	}
	return lError;
}

/*-----------------------------------------------------------*/

static int32_t lMbedtlsErrToTransportError( int32_t lError )
{
	switch( lError )
	{
	case MBEDTLS_ERR_X509_ALLOC_FAILED:
		lError = TLS_TRANSPORT_INSUFFICIENT_MEMORY;
		break;
	case MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED:
		lError = TLS_TRANSPORT_INTERNAL_ERROR;
		break;
	case MBEDTLS_ERR_X509_CERT_UNKNOWN_FORMAT:
	case MBEDTLS_ERR_X509_BAD_INPUT_DATA:
	case MBEDTLS_ERR_PEM_BAD_INPUT_DATA:
	case MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT:
		lError = TLS_TRANSPORT_PKI_OBJECT_PARSE_FAIL;
		break;
	default:
		lError = TLS_TRANSPORT_UNKNOWN_ERROR;
		break;

	}
	return lError;
}

/*-----------------------------------------------------------*/
#ifndef MBEDTLS_X509_REMOVE_INFO
#ifdef X509_CRT_ERROR_INFO
#undef X509_CRT_ERROR_INFO
#endif /* X509_CRT_ERROR_INFO */
#define X509_CRT_ERROR_INFO( err, err_str, info ) case err: pcVerifyInfo = info; break;
static const char * pcGetVerifyInfoString( int flag )
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


static void vLogCertificateVerifyResult( int flags )
{
#ifndef MBEDTLS_X509_REMOVE_INFO
	for( uint32_t mask = 1; mask != ( 1 << 31 ); mask = mask << 1 )
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

	for( ; pxCertName != NULL; pxCertName = pxCertName->MBEDTLS_PRIVATE( next ) )
	{
		if( MBEDTLS_OID_CMP( MBEDTLS_OID_AT_CN, &( pxCertName->MBEDTLS_PRIVATE( oid ) ) ) == 0 )
		{
			*ppucCommonName = pxCertName->MBEDTLS_PRIVATE( val ).MBEDTLS_PRIVATE( p );
			uxCommonNameLen = pxCertName->MBEDTLS_PRIVATE( val ).MBEDTLS_PRIVATE( len );
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
										  &( pxCert->MBEDTLS_PRIVATE( subject ) ) );

	uxIssuerCNLen = uxGetCertCNFromName( &pucIssuerCN,
										 &( pxCert->MBEDTLS_PRIVATE( issuer ) ) );

	uxSerialNumberLen = pxCert->MBEDTLS_PRIVATE( serial ).MBEDTLS_PRIVATE( len );
	pucSerialNumber = pxCert->MBEDTLS_PRIVATE( serial ).MBEDTLS_PRIVATE( p );
	for( uint32_t i = 0; i < uxSerialNumberLen; i++ )
	{
		if( i == 21 )
		{
			break;
		}
		snprintf( &( pcSerialNumberHex[ i * 2 ] ), 3, "%.02X", pucSerialNumber[ i ] );
	}

	pxValidFrom = &( pxCert->MBEDTLS_PRIVATE( valid_from ) );
	pxValidTo = &( pxCert->MBEDTLS_PRIVATE( valid_to ) );

	if( pcMessage && pucSubjectCN && pucIssuerCN && pucSerialNumber && pxValidFrom && pxValidTo )
	{
		LogInfo( "%s CN=%.*s, SN:0x%s", pcMessage, uxSubjectCNLen, pucSubjectCN, pcSerialNumberHex );
		LogInfo( "Issuer: CN=%.*s", uxIssuerCNLen, pucIssuerCN );
		LogInfo( "Valid From: %04d-%02d-%02d, Expires: %04d-%02d-%02d",
				 pxValidFrom->MBEDTLS_PRIVATE( year ), pxValidFrom->MBEDTLS_PRIVATE( mon ), pxValidFrom->MBEDTLS_PRIVATE( day ),
				 pxValidTo->MBEDTLS_PRIVATE( year ),   pxValidTo->MBEDTLS_PRIVATE( mon ),   pxValidTo->MBEDTLS_PRIVATE( day ) );
	}
}

/*-----------------------------------------------------------*/

static int32_t lAddCertificate( mbedtls_x509_crt * pxMbedtlsCertCtx,
								const PkiObject_t * pxCertificate,
								const TLSContext_t * pxTLSCtx )
{
	int32_t lError = TLS_TRANSPORT_PKI_OBJECT_NOT_FOUND;

    configASSERT( pxMbedtlsCertCtx != NULL );
    configASSERT( pxTLSCtx != NULL );
    configASSERT( pxCertificate != NULL );

    switch( pxCertificate->xForm )
    {
		case OBJ_FORM_PEM:
			lError = mbedtls_x509_crt_parse( pxMbedtlsCertCtx,
											 pxCertificate->pucBuffer,
											 pxCertificate->uxLen );
			if( lError != 0 )
			{
				LogError( "Failed to parse certificate from buffer: 0x%08X, length: %ld, Error: %s : %s.",
						  pxCertificate->pucBuffer, pxCertificate->uxLen,
						  mbedtlsHighLevelCodeOrDefault( lError ),
						  mbedtlsLowLevelCodeOrDefault( lError ) );
				lError = TLS_TRANSPORT_PKI_OBJECT_PARSE_FAIL;
			}
			break;
		case OBJ_FORM_DER:
		{
			lError = mbedtls_x509_crt_parse_der( pxMbedtlsCertCtx,
					 	 	 	 	 	 	 	 pxCertificate->pucBuffer,
												 pxCertificate->uxLen  );
			if( lError != 0 )
			{
				LogError( "Failed to parse certificate from buffer: 0x%08X, length: %ld, Error: %s : %s.",
						  pxCertificate->pucBuffer, pxCertificate->uxLen,
						  mbedtlsHighLevelCodeOrDefault( lError ),
						  mbedtlsLowLevelCodeOrDefault( lError ) );
				lError = TLS_TRANSPORT_PKI_OBJECT_PARSE_FAIL;
			}
			break;
		}
	#ifdef MBEDTLS_TRANSPORT_PKCS11
		case OBJ_FORM_PKCS11_LABEL:
		{
			lError = lReadCertificateFromPKCS11( pxMbedtlsCertCtx,
												 pxTLSCtx->xP11SessionHandle,
												 pxCertificate->pcPkcs11Label,
												 pxCertificate->uxLen );
			/* PKCS11 errors are > 0 */
			if( lError > 0 )
			{
				LogError( "Failed to read certificate(s) from pkcs11 label: %.*s, CK_RV: %s.",
						  pxCertificate->uxLen, pxCertificate->pcPkcs11Label, pcPKCS11StrError( lError ) );

				lError = lP11ErrToTransportError( lError );
			}
			/* Mbedtls errors are < 0 */
			if( lError < 0 )
			{
				LogError( "Failed to parse certificate(s) from pkcs11 label: %.*s, Error: %s : %s.",
						  pxCertificate->uxLen, pxCertificate->pcPkcs11Label,
						  mbedtlsHighLevelCodeOrDefault( lError ),
						  mbedtlsLowLevelCodeOrDefault( lError ) );
				lError = lMbedtlsErrToTransportError( lError );
			}
			break;
		}
	#endif
	#ifdef MBEDTLS_TRANSPORT_PSA
		case OBJ_FORM_PSA_CRYPTO:
			lError = lReadCertificateFromPSACrypto( pxMbedtlsCertCtx,
												    pxCertificate->xPsaCryptoId );
			if( lError != 0 )
			{
				LogError( "Failed to read certificate(s) from PSA Crypto uid: 0x%08X, Error: %s : %s.",
						  pxCertificate->xPsaCryptoId,
						  mbedtlsHighLevelCodeOrDefault( lError ),
						  mbedtlsLowLevelCodeOrDefault( lError ) );
			}
			break;

		case OBJ_FORM_PSA_ITS:
			lError = lReadCertificateFromPsaIts( pxMbedtlsCertCtx,
												 pxCertificate->xPsaStorageId );
			if( lError != 0 )
			{
				LogError( "Failed to read certificate(s) from PSA ITS uid: 0x%016X, lError: %s : %s.",
						  pxCertificate->xPsaStorageId,
						  mbedtlsHighLevelCodeOrDefault( lError ),
						  mbedtlsLowLevelCodeOrDefault( lError ) );
			}
			break;

		case OBJ_FORM_PSA_PS:
			lError = lReadCertificateFromPsaPS( pxMbedtlsCertCtx,
												pxCertificate->xPsaStorageId );
			if( lError != 0 )
			{
				LogError( "Failed to read certificate(s) from PSA PS uid: 0x%016X, lError: %s : %s.",
						  pxCertificate->xPsaStorageId,
						  mbedtlsHighLevelCodeOrDefault( lError ),
						  mbedtlsLowLevelCodeOrDefault( lError ) );
			}
			break;
	#endif
		case OBJ_FORM_NONE:
			/* Intentional fall through */
		default:
			LogError( "Invalid certificate form specified." );
			lError = TLS_TRANSPORT_INVALID_PARAMETER;
			break;
    }

    return lError;
}

/*-----------------------------------------------------------*/

static int32_t lLoadPrivateKey( mbedtls_pk_context * pxPkCtx,
								TLSContext_t * pxTLSCtx,
                                const PkiObject_t * pxPrivateKey )
{
    int32_t lError = -1;

	configASSERT( pxPkCtx != NULL );
    configASSERT( pxTLSCtx != NULL );
    configASSERT( pxPrivateKey != NULL );

    switch( pxPrivateKey->xForm )
    {
		case OBJ_FORM_PEM:
			/* Intentional fall through */
		case OBJ_FORM_DER:
		{
			configASSERT( pxPrivateKey->uxLen > 0 );
			configASSERT( pxPrivateKey->pucBuffer != NULL );

			lError = mbedtls_pk_parse_key( pxPkCtx,
										   pxPrivateKey->pucBuffer,
										   pxPrivateKey->uxLen,
										   NULL, 0,
										   pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( f_rng ),
										   pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( p_rng ) );
			if( lError != 0 )
			{
				LogError( "Failed to parse the client key: lError= %s : %s.",
							mbedtlsHighLevelCodeOrDefault( lError ),
							mbedtlsLowLevelCodeOrDefault( lError ) );
			}
			break;
		}
#ifdef MBEDTLS_TRANSPORT_PKCS11
		case OBJ_FORM_PKCS11_LABEL:
		{
			CK_OBJECT_HANDLE xPkHandle = CK_INVALID_HANDLE;
			CK_RV xResult = CKR_OK;

		    xResult = xFindObjectWithLabelAndClass( pxTLSCtx->xP11SessionHandle,
		    										pxPrivateKey->pcPkcs11Label,
													pxPrivateKey->uxLen,
													CKO_PRIVATE_KEY,
													&xPkHandle );

		    if( xResult != CKR_OK )
		    {
		    	LogError( "Failed to find private key in PKCS#11 module. CK_RV: %s",
		    			  pcPKCS11StrError( xResult ) );
		    	lError = lP11ErrToTransportError( xResult );
		    }
		    else
		    {
		    	lError = lPKCS11_initMbedtlsPkContext( pxPkCtx,
		    										   pxTLSCtx->xP11SessionHandle,
													   xPkHandle );

			    if( xResult != CKR_OK )
			    {
			    	LogError( "Failed to find private key in PKCS#11 module. CK_RV: %s",
			    			  pcPKCS11StrError( xResult ) );

			    	lError = lP11ErrToTransportError( xResult );
			    }
		    }
			break;
		}
#endif
#ifdef MBEDTLS_TRANSPORT_PSA
		case OBJ_FORM_PSA_CRYPTO:
		{
			lError = mbedtls_pk_setup_opaque( &( pxTLSCtx->xPkCtx ),
									          pxPrivateKey->xPsaCryptoId );
			if( lError != 0 )
			{
				LogError( "Failed to initialize the PSA opaque key context. lError= %s : %s.",
							mbedtlsHighLevelCodeOrDefault( lError ),
							mbedtlsLowLevelCodeOrDefault( lError ) );
			}
			break;
		}

		case OBJ_FORM_PSA_ITS:
		{
			unsigned char * pucPk = NULL;
			size_t uxPkLen = 0;
			lError = lLoadObjectFromPsaIts( &pucPk, &uxPkLen, pxPrivateKey->xPsaStorageId );

			if( lError != 0 )
			{
				LogError( "Failed to read the private key blob from the PSA ITS service. lError= %s : %s.",
						   mbedtlsHighLevelCodeOrDefault( lError ),
						   mbedtlsLowLevelCodeOrDefault( lError ) );
			}

			if( lError == 0 )
			{
				lError = mbedtls_pk_parse_key( pxPkCtx,
											   pucPk, uxPkLen,
											   NULL, 0,
											   pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( f_rng ),
											   pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( p_rng ) );

				if( lError != 0 )
				{
					LogError( "Failed to parse the private key. lError= %s : %s.",
							  mbedtlsHighLevelCodeOrDefault( lError ),
							  mbedtlsLowLevelCodeOrDefault( lError ) );
				}
			}

			if( pucPk != NULL )
			{
				configASSERT( uxPkLen > 0 );
				mbedtls_platform_zeroize( pucPk, uxPkLen );
				mbedtls_free( pucPk );
			}
			break;
		}
		case OBJ_FORM_PSA_PS:
		{
			unsigned char * pucPk = NULL;
			size_t uxPkLen = 0;
			lError = lLoadObjectFromPsaPs( &pucPk, &uxPkLen, pxPrivateKey->xPsaStorageId );

			if( lError != 0 )
			{
				LogError( "Failed to read the private key blob from the PSA PS service. lError= %s : %s.",
						   mbedtlsHighLevelCodeOrDefault( lError ),
						   mbedtlsLowLevelCodeOrDefault( lError ) );
			}

			if( lError == 0 )
			{
				lError = mbedtls_pk_parse_key( pxPkCtx,
											   pucPk, uxPkLen,
											   NULL, 0,
											   pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( f_rng ),
											   pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( p_rng ) );

				if( lError != 0 )
				{
					LogError( "Failed to parse the private key. lError= %s : %s.",
							  mbedtlsHighLevelCodeOrDefault( lError ),
							  mbedtlsLowLevelCodeOrDefault( lError ) );
				}
			}

			if( pucPk != NULL )
			{
				configASSERT( uxPkLen > 0 );
				mbedtls_platform_zeroize( pucPk, uxPkLen );
				mbedtls_free( pucPk );
			}
			break;
		}
#endif

		case OBJ_FORM_NONE:
			/* Intentional fallthrough */
		default:
			lError = TLS_TRANSPORT_INVALID_PARAMETER;
			break;
	}

    return lError;
}

/*-----------------------------------------------------------*/

static int mbedtls_ssl_send( void * pvCtx,
                             const unsigned char * pcBuf,
                             size_t xLen )
{
    SockHandle_t * pxSockHandle = ( SockHandle_t * ) pvCtx;
	int lError = 0;

	if( pxSockHandle == NULL ||
	    *pxSockHandle < 0 )
	{
		lError = MBEDTLS_ERR_NET_SOCKET_FAILED;
	}
	else
	{
		lError = sock_send( *pxSockHandle,
							( void * const ) pcBuf,
							xLen,
							0 );

		/* Map Error codes */
		if( lError < 0 )
		{
			/* force use of newlibc errno */
			switch( *__errno() )
			{
	#if EAGAIN != EWOULDBLOCK
				case EAGAIN:
	#endif
				case EINTR:
				case EWOULDBLOCK:
					lError = MBEDTLS_ERR_SSL_WANT_WRITE;
					break;
				case EPIPE:
				case ECONNRESET:
					lError = MBEDTLS_ERR_NET_CONN_RESET;
					break;
				default:
					lError = MBEDTLS_ERR_NET_SEND_FAILED;
					break;
			}
		}
	}
    return lError;
}

/*-----------------------------------------------------------*/

static int mbedtls_ssl_recv( void * pvCtx,
                             unsigned char * pcBuf,
                             size_t xLen )
{
    SockHandle_t * pxSockHandle = ( SockHandle_t * ) pvCtx;
    int lError = -1;

	if( pxSockHandle != NULL &&
	    *pxSockHandle >= 0 )
	{
		lError = sock_recv( *pxSockHandle,
						    ( void * ) pcBuf,
							xLen,
							0 );
	}

    if( lError < 0 )
    {
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

#ifdef TRANSPORT_USE_CTR_DRBG
		mbedtls_entropy_init( &( pxTLSCtx->xEntropyCtx ) );
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
			if( C_GetFunctionList( &pxFunctionList ) == CKR_OK &&
			    pxFunctionList->C_CloseSession != NULL )
			{
				pxFunctionList->C_CloseSession( pxTLSCtx->xP11SessionHandle );
			}
		}
#endif /* MBEDTLS_TRANSPORT_PKCS11 */

#ifdef TRANSPORT_USE_CTR_DRBG
		mbedtls_entropy_free( &( pxTLSCtx->xEntropyCtx ) );
		mbedtls_ctr_drbg_free( &( pxTLSCtx->xCtrDrbgCtx ) );
#endif /* TRANSPORT_USE_CTR_DRBG */
	}
}

/*-----------------------------------------------------------*/
#ifdef TRANSPORT_USE_CTR_DRBG
static int lEntropyCallback( void * pvCtx, unsigned char * pucBuffer, size_t uxLen,
                             size_t * puxOutLen )
{
	int lError = -1;
	configASSERT( pucBuffer != NULL );
	configASSERT_CONTINUE( uxLen > 0 );
	configASSERT( puxOutLen != NULL );

#ifdef MBEDTLS_TRANSPORT_PSA
	lError = lPSARandomCallback( NULL, pucBuffer, uxLen );

#elif defined( MBEDTLS_TRANSPORT_PKCS11)
	configASSERT( pvCtx != NULL );
	lError = lPKCS11RandomCallback( pvCtx, pucBuffer, uxLen );

#endif /* !MBEDTLS_TRANSPORT_PSA */

	if( lError == 0 )
	{
		*puxOutLen = uxLen;
	}

	return lError;
}
#endif /* TRANSPORT_USE_CTR_DRBG */
/*-----------------------------------------------------------*/

static int lConfigureEntropy( TLSContext_t * pxTLSCtx )
{
	int lRslt = 0;
	configASSERT( pxTLSCtx != NULL );

#ifdef TRANSPORT_USE_CTR_DRBG
#ifdef MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES
#ifdef MBEDTLS_TRANSPORT_PSA
	if( lRslt == 0 )
	{
		lRslt = mbedtls_entropy_add_source( &( pxTLSCtx->xEntropyCtx ),
	    							         lEntropyCallback, NULL,
	                                         MBEDTLS_ENTROPY_MIN_PLATFORM,
	                                         MBEDTLS_ENTROPY_SOURCE_STRONG );
		if( lRslt != 0 )
		{
			LogError( "Failed to add PSA entropy source: Error: %s : %s.",
		                    mbedtlsHighLevelCodeOrDefault( lError ),
		                    mbedtlsLowLevelCodeOrDefault( lError ) );
		}
	}
#elif MBEDTLS_TRANSPORT_PKCS11
	if( lRslt == 0 )
	{
		lRslt = mbedtls_entropy_add_source( &( pxTLSCtx->xEntropyCtx ),
	    							        lEntropyCallback, NULL,
	                                        MBEDTLS_ENTROPY_MIN_PLATFORM,
	                                        MBEDTLS_ENTROPY_SOURCE_STRONG );

		if( lRslt != 0 )
		{
			LogError( "Failed to add PKCS#11 entropy source: Error: %s : %s.",
		                    mbedtlsHighLevelCodeOrDefault( lError ),
		                    mbedtlsLowLevelCodeOrDefault( lError ) );
		}
	}
#else /* MBEDTLS_TRANSPORT_PKCS11 */
	#error "No default entropy source defined."
#endif /* !MBEDTLS_TRANSPORT_PKCS11 && !MBEDTLS_TRANSPORT_PSA */
#endif /* MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES */

#endif /* TRANSPORT_USE_CTR_DRBG */

	/* TODO: perform entropy self-test: mbedtls_entropy_source_self_test */

#ifdef TRANSPORT_USE_CTR_DRBG
	if( lRslt == 0 )
	{
		/* Seed the local RNG. */
		lRslt = mbedtls_ctr_drbg_seed( pCtrDrgbContext,
									   mbedtls_entropy_func,
									   pEntropyContext,
									   NULL,
									   0 );
		if( lError != 0 )
		{
			LogError( "Failed to seed PRNG: Error: %s : %s.",
					  mbedtlsHighLevelCodeOrDefault( lError ),
					  mbedtlsLowLevelCodeOrDefault( lError ) );
			xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
		}
	}

	/* Configure RNG callback */
	mbedtls_ssl_conf_rng( &( pxTLSCtx->xSslConfig ),
	                      mbedtls_ctr_drbg_random,
	                      &( pxTLSCtx->xCtrDrbgCtx ) );

#elif defined( MBEDTLS_TRANSPORT_PSA )
	mbedtls_ssl_conf_rng( &( pxTLSCtx->xSslConfig ),
						  lPSARandomCallback,
						  NULL );

#elif defined( MBEDTLS_TRANSPORT_PKCS11 )
	mbedtls_ssl_conf_rng( &( pxTLSCtx->xSslConfig ),
						  lPKCS11RandomCallback,
						  &( pxTLSCtx->xP11SessionHandle ) );

#else
	LogError( "Transport entropy configurations is invalid." );
	xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
#endif /* !MBEDTLS_TRANSPORT_PKCS11 && !MBEDTLS_TRANSPORT_PSA && !TRANSPORT_USE_CTR_DRBG */

	return lRslt;
}

static int lValidateCertByProfile( TLSContext_t * pxTLSCtx, mbedtls_x509_crt * pxCert )
{
	int lFlags = 0;
	const mbedtls_x509_crt_profile * pxCertProfile = NULL;

	if( pxTLSCtx == NULL || pxCert == NULL )
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
#if defined(MBEDTLS_RSA_C)
		if( mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_RSA ||
			mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_RSASSA_PSS )
		{
			if( mbedtls_pk_get_bitlen( pxPkCtx ) < pxCertProfile->rsa_min_bitlen )
			{
				lFlags |= MBEDTLS_X509_BADCERT_BAD_KEY;
			}
		}
#endif /* MBEDTLS_RSA_C */

#if defined(MBEDTLS_ECP_C)
		if( mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_ECDSA ||
			mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_ECKEY ||
			mbedtls_pk_get_type( pxPkCtx ) == MBEDTLS_PK_ECKEY_DH )
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

static const int plCipherSuites = { MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 , 0 };

/*-----------------------------------------------------------*/
TlsTransportStatus_t mbedtls_transport_configure( NetworkContext_t * pxNetworkContext,
												  const char ** ppcAlpnProtos,
												  const PkiObject_t * pxPrivateKey,
												  const PkiObject_t * pxClientCert,
												  const PkiObject_t * pxRootCaCerts,
												  const size_t uxNumRootCA )
{
	TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;
	char * pcMbedtlsPrintBuffer = NULL;

	TlsTransportStatus_t xStatus = TLS_TRANSPORT_SUCCESS;
	int lError = 0;

	if( pxNetworkContext == NULL )
	{
		LogError( "Provided pxNetworkContext cannot be NULL." );
		xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
	}
	else if( pxPrivateKey == NULL )
	{
		LogError( "Provided pxPrivateKey cannot be NULL." );
		xStatus = TLS_TRANSPORT_INVALID_PARAMETER;
	}
	else if( pxClientCert == NULL )
	{
		LogError( "Provided pxClientCert cannot be NULL." );
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
		/* Empty */
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
#endif  /* MBEDTLS_TRANSPORT_PKCS11 */

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

		/* Setup entropy / rng contexts */
		if( xStatus == TLS_TRANSPORT_SUCCESS )
		{
			lError = lConfigureEntropy( pxTLSCtx );

			if( lError != 0 )
			{
				xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
			}
		}
	}

	configASSERT( pxTLSCtx->xConnectionState != STATE_CONNECTED );

	if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
	{
		/* Clear any existing pk and cert contexts */
		mbedtls_x509_crt_free( &( pxTLSCtx->xRootCaChain ) );
		mbedtls_x509_crt_free( &( pxTLSCtx->xClientCert ) );
		mbedtls_pk_free( &( pxTLSCtx->xPkCtx ) );
	}

	if( xStatus == TLS_TRANSPORT_SUCCESS )
	{
#ifdef MBEDTLS_DEBUG_C
		mbedtls_ssl_conf_dbg( &( pxTLSCtx->xSslConfig ), vTLSDebugPrint, NULL );
		mbedtls_debug_set_threshold( MBEDTLS_DEBUG_THRESHOLD );
#endif  /* MBEDTLS_DEBUG_C */

		if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
		{
			mbedtls_ssl_config_free( &( pxTLSCtx->xSslConfig ) );
			mbedtls_ssl_config_init( &( pxTLSCtx->xSslConfig ) );
		}

		/* Initialize SSL Config from defaults */
		lError = mbedtls_ssl_config_defaults( &( pxTLSCtx->xSslConfig ),
											  MBEDTLS_SSL_IS_CLIENT,
											  MBEDTLS_SSL_TRANSPORT_STREAM,
											  MBEDTLS_SSL_PRESET_DEFAULT );

		if( lError != 0 )
		{
			LogError( "Failed to initialize ssl configuration: Error: %s : %s.",
					   mbedtlsHighLevelCodeOrDefault( lError ),
					   mbedtlsLowLevelCodeOrDefault( lError ) );
			xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
		}
		else
		{
			/* Set minimum ssl / tls version */
			mbedtls_ssl_conf_min_version( &( pxTLSCtx->xSslConfig ),
										  MBEDTLS_SSL_MAJOR_VERSION_3,
			                              MBEDTLS_SSL_MINOR_VERSION_3 );

			mbedtls_ssl_conf_cert_profile( &( pxTLSCtx->xSslConfig ),
			                               &mbedtls_x509_crt_profile_default );

			mbedtls_ssl_conf_authmode( &( pxTLSCtx->xSslConfig ),
			                           MBEDTLS_SSL_VERIFY_REQUIRED );

//			mbedtls_ssl_conf_ciphersuites( &( pxTLSCtx->xSslConfig ), plCipherSuites );
		}
	}

	/* Configure ALPN Protocols */
	if( xStatus == TLS_TRANSPORT_SUCCESS &&
		ppcAlpnProtos != NULL )
	{
        /* Include an application protocol list in the TLS ClientHello
         * message. */
        lError = mbedtls_ssl_conf_alpn_protocols( &( pxTLSCtx->xSslConfig ),
        										  ppcAlpnProtos );

        if( lError != 0 )
        {
        	LogError( "Failed to configure ALPN protocols: Error: %s : %s.",
        	          mbedtlsHighLevelCodeOrDefault( lError ),
        	          mbedtlsLowLevelCodeOrDefault( lError ) );
        }
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
        lError = mbedtls_ssl_conf_max_frag_len( &( pxTLSCtx->xSslConfig ),
        										MBEDTLS_SSL_MAX_FRAG_LEN_4096 );

        if( lError != 0 )
        {
            LogError( "Failed to configure maximum fragment length extension: Error: %s : %s.",
                      mbedtlsHighLevelCodeOrDefault( lError ),
                      mbedtlsLowLevelCodeOrDefault( lError ) );
        }
	}
#endif /* MBEDTLS_SSL_MAX_FRAGMENT_LENGTH */

	/* Initialize private key */
	if( xStatus == TLS_TRANSPORT_SUCCESS )
	{
		if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
		{
			mbedtls_pk_free( &( pxTLSCtx->xPkCtx ) );
			mbedtls_pk_init( &( pxTLSCtx->xPkCtx ) );
		}

	    lError = lLoadPrivateKey( &( pxTLSCtx->xPkCtx ), pxTLSCtx, pxPrivateKey );
		if( lError != 0 )
		{
			LogError( "Client private key load operation failed: Error: %s : %s.",
					  mbedtlsHighLevelCodeOrDefault( lError ),
					  mbedtlsLowLevelCodeOrDefault( lError ) );

			xStatus = TLS_TRANSPORT_CLIENT_KEY_INVALID;
		}
		else
		{
			xStatus = TLS_TRANSPORT_SUCCESS;
		}
	}

	if( xStatus == TLS_TRANSPORT_SUCCESS )
	{
		if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
		{
			mbedtls_x509_crt_free( &( pxTLSCtx->xClientCert ) );
			mbedtls_x509_crt_init( &( pxTLSCtx->xClientCert ) );
		}

		xStatus = lAddCertificate( &( pxTLSCtx->xClientCert ),
								   pxClientCert, pxTLSCtx );

		if( xStatus != TLS_TRANSPORT_SUCCESS )
		{
			LogError( "Failed to add client certificate to TLS context." );
		}
	}

	if( xStatus == TLS_TRANSPORT_SUCCESS )
	{
//		unsigned char * pcCommonName = NULL;
//		size_t uxCommonNameLen = uxGetCertCNFromName( &pcCommonName, &( pxTLSCtx->xClientCert.MBEDTLS_PRIVATE( subject ) ) );
//
//		if( pcCommonName != NULL && uxCommonNameLen > 0 )
//		{
//			LogInfo( "Loaded client certificate CN=%.*s", uxCommonNameLen, pcCommonName );
//		}

		lError = lValidateCertByProfile( pxTLSCtx, &( pxTLSCtx->xClientCert ) );

		if( lError != 0 )
		{
			vLogCertificateVerifyResult( lError );

			xStatus = TLS_TRANSPORT_CLIENT_CERT_INVALID;
		}
		else
		{
			vLogCertInfo( &( pxTLSCtx->xClientCert ), "Client Certificate:" );
		}
	}

	//TODO: Validate key / cert are same key type

	/* Validate that the cert and pk match. */
	if( xStatus == TLS_TRANSPORT_SUCCESS )
	{
		/* Construct a temporary key context so that public key and priate key pk_info pointers match */
		mbedtls_pk_context xTempPubKeyCtx;
		xTempPubKeyCtx.MBEDTLS_PRIVATE( pk_ctx ) = pxTLSCtx->xClientCert.MBEDTLS_PRIVATE( pk ).MBEDTLS_PRIVATE( pk_ctx );
		xTempPubKeyCtx.MBEDTLS_PRIVATE( pk_info ) = pxTLSCtx->xPkCtx.MBEDTLS_PRIVATE( pk_info );

		lError = mbedtls_pk_check_pair( &xTempPubKeyCtx,
										&( pxTLSCtx->xPkCtx ),
										pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( f_rng ),
										pxTLSCtx->xSslConfig.MBEDTLS_PRIVATE( p_rng ) );

		if( lError != 0 )
		{
			LogError( "Public-Private keypair does not match the provided certificate. Error: %s : %s",
					  mbedtlsHighLevelCodeOrDefault( lError ),
					  mbedtlsLowLevelCodeOrDefault( lError ) );
			xStatus = TLS_TRANSPORT_INVALID_CREDENTIALS;
		}
	}

	if( xStatus == TLS_TRANSPORT_SUCCESS )
	{
        lError = mbedtls_ssl_conf_own_cert( &( pxTLSCtx->xSslConfig ),
                                            &( pxTLSCtx->xClientCert ),
                                            &( pxTLSCtx->xPkCtx ) );
        if( lError != 0 )
		{
        	LogError( "Failed to configure TLS client certificate. Error: %s : %s.",
        	          mbedtlsHighLevelCodeOrDefault( lError ),
        	          mbedtlsLowLevelCodeOrDefault( lError ) );
			xStatus = TLS_TRANSPORT_INVALID_CREDENTIALS;
		}
	}

	if( xStatus == TLS_TRANSPORT_SUCCESS )
	{
		mbedtls_x509_crt * pxRootCertIterator = NULL;
		size_t uxValidRootCerts = 0;

		if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
		{
			mbedtls_x509_crt_free( &( pxTLSCtx->xRootCaChain ) );
			mbedtls_x509_crt_init( &( pxTLSCtx->xRootCaChain ) );
		}

		for( size_t uxIdx = 0; uxIdx < uxNumRootCA; uxIdx++ )
		{
			const PkiObject_t * pxRootCert = &( pxRootCaCerts[ uxIdx ] );
			mbedtls_x509_crt * pxTempCaCert = NULL;

			if( pxRootCertIterator == NULL )
			{
				pxTempCaCert = &( pxTLSCtx->xRootCaChain );
			}
			else /* Heap allocate all but the first mbedtls_x509_crt object */
			{
				pxTempCaCert = mbedtls_calloc( 1, sizeof( mbedtls_x509_crt ) );
			}

			/* If heap allocation failed, break out of loop */
			if( pxTempCaCert == NULL )
			{
				LogError( "Failed to allocate memory for mbedtls_x509_crt object." );
				lError = MBEDTLS_ERR_X509_ALLOC_FAILED;
				break;
			}
			else
			{
				mbedtls_x509_crt_init( pxTempCaCert );

				/* load the certificate onto the heap */
				lError = lAddCertificate( pxTempCaCert, pxRootCert, pxTLSCtx );

				if( lError != 0 )
				{
					LogError( "Failed to load the CA Certificate at index: %ld, Error: %s : %s.",
							  uxIdx,
							  mbedtlsHighLevelCodeOrDefault( lError ),
							  mbedtlsLowLevelCodeOrDefault( lError ) );
				}
			}

			if( lError == 0 )
			{
				lError = lValidateCertByProfile( pxTLSCtx, &( pxTLSCtx->xClientCert ) );
				if( lError != 0 )
				{
#if !defined( MBEDTLS_X509_REMOVE_INFO )
					int lMbedtlsInfoLen = 0;

					if( pcMbedtlsPrintBuffer == NULL )
					{
						pcMbedtlsPrintBuffer = pvPortMalloc( 256 );
					}

					if( pcMbedtlsPrintBuffer )
					{
						lMbedtlsInfoLen = mbedtls_x509_crt_verify_info( pcMbedtlsPrintBuffer, 256,
																		 "", lError );
					}

					if( lMbedtlsInfoLen > 0 )
					{
						LogError( "Failed to validate the CA Certificate at index: %ld. Reason: %.*s", uxIdx,
								  lMbedtlsInfoLen, pcMbedtlsPrintBuffer );
					}
					else
#endif /* !defined( MBEDTLS_X509_REMOVE_INFO ) */
					{
						LogError( "Failed to validate the CA Certificate at index: %ld.", uxIdx );
					}
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
				uxValidRootCerts++;
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
				xStatus = lMbedtlsErrToTransportError( lError );
				break;
			}
		}

		if( uxValidRootCerts > 0 )
		{
			mbedtls_ssl_conf_ca_chain( &( pxTLSCtx->xSslConfig ),
									   &( pxTLSCtx->xRootCaChain ),
									   NULL );
		}
		else if( lError == MBEDTLS_ERR_X509_ALLOC_FAILED )
		{
			LogError( "Heap memory allocation failed." );
		}
		else
		{
			LogError( "Failed to load any valid Root CA Certificates." );
			xStatus = TLS_TRANSPORT_NO_VALID_CA_CERT;
		}
	}

	/* Initialize SSL context */
    if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
    	/* Clear the ssl connection context if we're reconfiguring */
		if( pxTLSCtx->xConnectionState == STATE_CONFIGURED )
		{
			mbedtls_ssl_free( &( pxTLSCtx->xSslCtx ) );
			mbedtls_ssl_init( &( pxTLSCtx->xSslCtx ) );
		}

    	/* Setup tls connection context and associate it with the tls config. */
    	lError = mbedtls_ssl_setup( &( pxTLSCtx->xSslCtx ),
    	                            &( pxTLSCtx->xSslConfig ) );

    	if( lError != 0 )
    	{
            LogError( "Failed to set up mbed TLS SSL context: Error: %s : %s.",
                      mbedtlsHighLevelCodeOrDefault( lError ),
                      mbedtlsLowLevelCodeOrDefault( lError ) );

            xStatus = TLS_TRANSPORT_INTERNAL_ERROR;
    	}
		else
		{
			/* Setup mbedtls IO callbacks */
			mbedtls_ssl_set_bio( &( pxTLSCtx->xSslCtx ),
								 &( pxTLSCtx->xSockHandle ),
								 mbedtls_ssl_send,
								 mbedtls_ssl_recv,
								 NULL );

			pxTLSCtx->xConnectionState = STATE_CONFIGURED;
		}

    	if( lError == 0 )
    	{
			mbedtls_ssl_set_hs_ca_chain( &( pxTLSCtx->xSslCtx ),
										 &( pxTLSCtx->xRootCaChain ),
										 NULL );
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
        	.ai_family = AF_INET,
    		.ai_socktype = SOCK_STREAM,
    		.ai_protocol = IPPROTO_TCP,
        };

    	lError = dns_getaddrinfo( pcHostName, NULL,
    							  &xAddrInfoHint, &pxAddrInfo );

    	if( lError != 0 || pxAddrInfo == NULL )
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
					( ( struct sockaddr_in * )pxAddrIter->ai_addr )->sin_port = htons( usPort );
					break;
#endif
#if LWIP_IPV6 == 1
				case AF_INET6:
					( ( struct sockaddr_in6 * )pxAddrIter->ai_addr )->sin6_port = htons( usPort );
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
				( void ) inet_ntoa_r( pxAddrIter->ai_addr, ipAddrBuff, IP4ADDR_STRLEN_MAX );

				LogInfo( "Trying address: %.*s, port: %uh for host: %s.",
						 IP4ADDR_STRLEN_MAX, ipAddrBuff, usPort, pcHostName );
			}
#endif
#if LWIP_IPV6 == 1
			if( pxAddrIter->ai_family == AF_INET6 )
			{
				char ipAddrBuff[ IP6ADDR_STRLEN_MAX ] = { 0 };
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
						( void ) inet_ntoa_r( pxAddrIter->ai_addr, ipAddrBuff, IP4ADDR_STRLEN_MAX );

						LogInfo( "Connected socket: %ld to host: %s, address: %.*s, port: %uh.",
								 pxTLSCtx->xSockHandle, pcHostName,
								 IP4ADDR_STRLEN_MAX, ipAddrBuff, usPort );
					}
#endif
#if LWIP_IPV6 == 1
					if( pxAddrIter->ai_family == AF_INET6 )
					{
						char ipAddrBuff[ IP6ADDR_STRLEN_MAX ] = { 0 };
						LogInfo( "Connected socket: %ld to host: %s, address: %.*s, port: %uh.",
								 pxTLSCtx->xSockHandle, pcHostName,
								 IP6ADDR_STRLEN_MAX, ipAddrBuff, usPort );
					}
#endif
				}
			}

			/* Exit loop on an irrecoverable error or successful connection. */
			if( xStatus != TLS_TRANSPORT_SUCCESS ||
				pxTLSCtx->xSockHandle >= 0 )
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

	if( xStatus == TLS_TRANSPORT_SUCCESS &&
		pxTLSCtx->xSockHandle < 0 )
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
    int lError = 0;
    TLSContext_t * pxTLSCtx = ( TLSContext_t * ) pxNetworkContext;

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
		/* Empty */
	}

	/* Set hostname for SNI and server certificate verification */
	if( xStatus == TLS_TRANSPORT_SUCCESS &&
		( pxTLSCtx->xSslCtx.MBEDTLS_PRIVATE( hostname ) == NULL ||
		  strncmp( pxTLSCtx->xSslCtx.MBEDTLS_PRIVATE( hostname ), pcHostName,  MBEDTLS_SSL_MAX_HOST_NAME_LEN ) != 0 ) )
	{
		lError = mbedtls_ssl_set_hostname( &( pxTLSCtx->xSslCtx ), pcHostName );

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

	if( xStatus == TLS_TRANSPORT_SUCCESS &&
		ulRecvTimeoutMs == 0 )
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
            lError = mbedtls_ssl_handshake( &( pxTLSCtx->xSslCtx ) );
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
            LogInfo( "(Network connection %p) TLS handshake successful.",
                     pxTLSCtx );
        }
	}

	if( xStatus == TLS_TRANSPORT_SUCCESS )
    {
        LogInfo( "(Network connection %p) Connection to %s:%u established.",
                   pxNetworkContext, pcHostName, usPort );

		pxTLSCtx->xConnectionState = STATE_CONNECTED;
	}
	else
	{
		/* Clean up on failure. */
		if( pxNetworkContext != NULL &&
			pxTLSCtx->xSockHandle >= 0 )
		{
			/* Deallocate the open socket. */
			sock_close( pxTLSCtx->xSockHandle );
			pxTLSCtx->xSockHandle = -1;
		}

		/* Reset SSL session context for reconnect attempt */
		mbedtls_ssl_session_reset( &( pxTLSCtx->xSslCtx ) );

		LogInfo( "(Network connection %p) to %s:%u failed.",
                   pxNetworkContext,
                   pcHostName, usPort );
	}

    return xStatus;
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

	if( pxTLSCtx != NULL &&
	    pxTLSCtx->xSockHandle >= 0 )
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
					LogInfo( "(Network connection %p) TLS close-notify sent.",
							pxNetworkContext );
				}
				else
				{
					LogError( "(Network connection %p) Failed to send TLS close-notify: Error: %s : %s.",
								pxNetworkContext,
								mbedtlsHighLevelCodeOrDefault( tlsStatus ),
								mbedtlsLowLevelCodeOrDefault( tlsStatus ) );
				}
			}
			pxTLSCtx->xConnectionState = STATE_CONFIGURED;
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
	else if( tlsStatus == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
			 tlsStatus == MBEDTLS_ERR_NET_CONN_RESET )
	{
		tlsStatus = -1;
		pxTLSCtx->xConnectionState = STATE_CONFIGURED;
		if( pxTLSCtx->xSockHandle >= 0 )
		{
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
        /* Empty else marker. */
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
        LogDebug( "Failed to send data. However, send can be retried on this error. "
                    "Error: %s : %s.",
                    mbedtlsHighLevelCodeOrDefault( tlsStatus ),
                    mbedtlsLowLevelCodeOrDefault( tlsStatus ) );

        /* Mark these set of errors as a timeout. The libraries may retry send
         * on these errors. */
        tlsStatus = 0;
    }
	/* Close the Socket if needed. */
	else if( tlsStatus == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
			 tlsStatus == MBEDTLS_ERR_NET_CONN_RESET )
	{
		tlsStatus = -1;
		pxTLSCtx->xConnectionState = STATE_CONFIGURED;
		if( pxTLSCtx->xSockHandle >= 0 )
		{
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


