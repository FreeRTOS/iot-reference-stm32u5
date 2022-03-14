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

/**
 * @file mbedtls_pk_psa.c
 * @brief mbedtls_pk implementation for psa ECDSA keys.
 *           Exports a mbedtls_pk_info_t type.
 */

#ifdef MBEDTLS_TRANSPORT_PSA
#include <string.h>

#include "mbedtls_transport.h"

/* Mbedtls Includes */
#include "mbedtls/private_access.h"

#include "mbedtls/pk.h"
#include "mbedtls/asn1.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform.h"
#include "mbedtls/asn1write.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/psa_util.h"
#include "pk_wrap.h"

#include "psa/crypto.h"
#include "psa/internal_trusted_storage.h"
#include "psa/protected_storage.h"

//typedef struct PsaEcDsaCtx
//{
//	mbedtls_ecdsa_context xMbedEcDsaCtx;
//	psa_key_id_t xKeyId;
//} PsaEcDsaCtx_t;


//static void * psa_ecdsa_ctx_alloc( void );
//
//static CK_RV psa_ecdsa_ctx_init( void * pvCtx,
//								 CK_FUNCTION_LIST_PTR pxFunctionList,
//								 CK_SESSION_HANDLE xSessionHandle,
//								 CK_OBJECT_HANDLE xPkHandle );
//
//static void psa_ecdsa_ctx_free( void * pvCtx );
//
//static int psa_ecdsa_sign( void * pvCtx, mbedtls_md_type_t xMdAlg,
//                           const unsigned char * pucHash, size_t xHashLen,
//                           unsigned char * pucSig, size_t xSigBufferSize, size_t * pxSigLen,
//                           int (* plRng)(void *, unsigned char *, size_t),
//                           void * pvRng );
//
//static size_t psa_ecdsa_get_bitlen( const void * pvCtx );
//
//static int psa_ecdsa_can_do( mbedtls_pk_type_t xType );
//
//static int psa_ecdsa_verify( void * pvCtx, mbedtls_md_type_t xMdAlg,
//                             const unsigned char * pucHash, size_t xHashLen,
//							 const unsigned char * pucSig, size_t xSigLen );
//
//static int psa_ecdsa_check_pair( const void * pvPub, const void * pvPrv,
//                                 int (* lFRng)(void *, unsigned char *, size_t),
//                                 void * pvPRng );
//
//static void psa_ecdsa_debug( const void * pvCtx, mbedtls_pk_debug_item * pxItems );

//mbedtls_pk_info_t mbedtls_psa_pk_ecdsa =
//{
//    .type = MBEDTLS_PK_OPAQUE,
//    .name = "PSA",
//    .get_bitlen = psa_ecdsa_get_bitlen,
//    .can_do = psa_ecdsa_can_do,
//    .verify_func = psa_ecdsa_verify,
//    .sign_func = psa_ecdsa_sign,
//#if defined(MBEDTLS_ECDSA_C) && defined(MBEDTLS_ECP_RESTARTABLE)
//    .verify_rs_func = NULL,
//    .sign_rs_func = NULL,
//#endif /* MBEDTLS_ECDSA_C && MBEDTLS_ECP_RESTARTABLE */
//    .decrypt_func = NULL,
//    .encrypt_func = NULL,
//    .check_pair_func = psa_ecdsa_check_pair,
//    .ctx_alloc_func = psa_ecdsa_ctx_alloc,
//    .ctx_free_func = psa_ecdsa_ctx_free,
//#if defined(MBEDTLS_ECDSA_C) && defined(MBEDTLS_ECP_RESTARTABLE)
//    .rs_alloc_func = NULL,
//    .rs_free_func = NULL,
//#endif /* MBEDTLS_ECDSA_C && MBEDTLS_ECP_RESTARTABLE */
//    .debug_func = psa_ecdsa_debug,
//};


int lPSARandomCallback( void * pvCtx, unsigned char * pucOutput,
						size_t uxLen )
{
	int lRslt = 0;
	( void ) pvCtx;

	lRslt = psa_generate_random( pucOutput, uxLen );

	return lRslt;
}

/*-----------------------------------------------------------*/
int32_t lReadCertificateFromPSACrypto( mbedtls_x509_crt * pxCertificateContext,
									   psa_key_id_t xCertId )
{
	psa_status_t xStatus = PSA_SUCCESS;
	psa_key_attributes_t xCertAttrs = PSA_KEY_ATTRIBUTES_INIT;
	uint8_t * pucCertBuffer = NULL;
	size_t uxBufferLen = 0;
	size_t uxCertLen = 0;

	configASSERT( xCertId >= PSA_KEY_ID_USER_MIN );
	configASSERT( xCertId <= PSA_KEY_ID_VENDOR_MAX );

	if( pxCertificateContext == NULL )
	{
		xStatus = PSA_ERROR_INVALID_ARGUMENT;
	}

	/* Fetch key attributes to validate the storage id. */
	xStatus = psa_get_key_attributes( xCertId, &xCertAttrs );

	/* Check that the key type is "raw" */
	if( xStatus == PSA_SUCCESS &&
		psa_get_key_type( &xCertAttrs ) != PSA_KEY_TYPE_RAW_DATA )
	{
		xStatus = PSA_ERROR_INVALID_HANDLE;
	}

	/* Determine length of buffer needed */
	if( xStatus == PSA_SUCCESS )
	{
		if( psa_get_key_bits( &xCertAttrs ) > 0 )
		{
			uxBufferLen = psa_get_key_bits( &xCertAttrs ) * 8;
		}
		else
		{
			xStatus = PSA_ERROR_INVALID_HANDLE;
		}
	}

	if( xStatus == PSA_SUCCESS )
	{
		pucCertBuffer = mbedtls_calloc( 1, uxBufferLen );

		if( pucCertBuffer == NULL )
		{
			xStatus = PSA_ERROR_INSUFFICIENT_MEMORY;
		}
	}


	if( xStatus == PSA_SUCCESS )
	{
		xStatus = psa_export_key( xCertId,
								  pucCertBuffer,
								  uxBufferLen,
								  &uxCertLen );
	}

	if( xStatus == PSA_SUCCESS )
	{
		xStatus = mbedtls_x509_crt_parse( pxCertificateContext,
		        				          pucCertBuffer,
										  uxCertLen );
	}
	else
	{
		xStatus = mbedtls_psa_err_translate_pk( xStatus );
	}

    /* Free memory. */
	if( pucCertBuffer != NULL )
	{
		mbedtls_free( pucCertBuffer );
	}

    return xStatus;
}

int32_t lLoadObjectFromPsaIts( uint8_t ** ppucData,
							   size_t * puxDataLen,
							   psa_storage_uid_t xObjectUid )
{
	psa_status_t xStatus = PSA_SUCCESS;
	struct psa_storage_info_t xStorageInfo = { 0 };
	void * pvDataBuffer = NULL;
	size_t uxDataLen = 0;

	configASSERT( xObjectUid > 0 );

	if( ppucData == NULL )
	{
		xStatus = PSA_ERROR_INVALID_ARGUMENT;
	}


	if( xStatus == PSA_SUCCESS )
	{
		/* Fetch key attributes to validate the storage id. */
		xStatus = psa_its_get_info( xObjectUid, &xStorageInfo );
	}

	if( xStatus == PSA_SUCCESS )
	{
		pvDataBuffer = mbedtls_calloc( 1, xStorageInfo.size );

		if( pvDataBuffer == NULL )
		{
			xStatus = PSA_ERROR_INSUFFICIENT_MEMORY;
		}
	}

	if( xStatus == PSA_SUCCESS )
	{
		xStatus = psa_its_get( xObjectUid,
							   0,
							   xStorageInfo.size,
							   pvDataBuffer,
							   &uxDataLen );
	}

	if( xStatus == PSA_SUCCESS )
	{
		*ppucData = ( unsigned char * ) pvDataBuffer;

		if( puxDataLen != NULL )
		{
			*puxDataLen = uxDataLen;
		}
	}

	return mbedtls_psa_err_translate_pk( xStatus );
}

int32_t lLoadObjectFromPsaPs( uint8_t ** ppucData,
							  size_t * puxDataLen,
							  psa_storage_uid_t xObjectUid )
{
	psa_status_t xStatus = PSA_SUCCESS;
	struct psa_storage_info_t xStorageInfo = { 0 };
	void * pvDataBuffer = NULL;
	size_t uxDataLen = 0;

	configASSERT( xObjectUid > 0 );

	if( ppucData == NULL )
	{
		xStatus = PSA_ERROR_INVALID_ARGUMENT;
	}


	if( xStatus == PSA_SUCCESS )
	{
		/* Fetch key attributes to validate the storage id. */
		xStatus = psa_ps_get_info( xObjectUid, &xStorageInfo );
	}

	if( xStatus == PSA_SUCCESS )
	{
		pvDataBuffer = mbedtls_calloc( 1, xStorageInfo.size );

		if( pvDataBuffer == NULL )
		{
			xStatus = PSA_ERROR_INSUFFICIENT_MEMORY;
		}
	}

	if( xStatus == PSA_SUCCESS )
	{
		xStatus = psa_ps_get( xObjectUid,
							  0,
							  xStorageInfo.size,
							  pvDataBuffer,
							  &uxDataLen );
	}

	if( xStatus == PSA_SUCCESS )
	{
		*ppucData = ( unsigned char * )pvDataBuffer;

		if( puxDataLen != NULL )
		{
			*puxDataLen = uxDataLen;
		}
	}

	return mbedtls_psa_err_translate_pk( xStatus );
}

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
		lError = lLoadObjectFromPsaIts( &pucCertBuffer, &uxCertLen, xCertUid );
	}

	if( lError == 0 )
	{
		/* Determine if certificate is in pem or der format */
		if( uxCertLen != 0 && pucCertBuffer[ uxCertLen - 1] == '\0' &&
		    strstr( (const char *) pucCertBuffer, "-----BEGIN CERTIFICATE-----" ) != NULL )
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
		lError = lLoadObjectFromPsaPs( &pucCertBuffer, &uxCertLen, xCertUid );
	}

	if( lError == 0 )
	{
		/* Determine if certificate is in pem or der format */
		if( uxCertLen != 0 && pucCertBuffer[ uxCertLen - 1] == '\0' &&
		    strstr( (const char *) pucCertBuffer, "-----BEGIN CERTIFICATE-----" ) != NULL )
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

//mbedtls_pk_setup_opaque


//
//int32_t lPKCS11_initMbedtlsPkContext( mbedtls_pk_context * pxMbedtlsPkCtx,
//									  CK_SESSION_HANDLE xSessionHandle,
//									  CK_OBJECT_HANDLE xPkHandle )
//{
//	CK_RV xResult = CKR_OK;
//
//	CK_KEY_TYPE xKeyType = CKK_VENDOR_DEFINED;
//	CK_FUNCTION_LIST_PTR pxFunctionList = NULL;
//
//	if( pxMbedtlsPkCtx == NULL )
//	{
//		xResult = CKR_ARGUMENTS_BAD;
//	}
//	else if( xSessionHandle == CK_INVALID_HANDLE )
//	{
//		xResult = CKR_SESSION_HANDLE_INVALID;
//	}
//	else if( xPkHandle == CK_INVALID_HANDLE )
//	{
//		xResult = CKR_KEY_HANDLE_INVALID;
//	}
//	else if( C_GetFunctionList( &pxFunctionList ) != CKR_OK ||
//			 pxFunctionList == NULL ||
//			 pxFunctionList->C_GetAttributeValue == NULL )
//	{
//		xResult = CKR_FUNCTION_FAILED;
//	}
//	/* Determine key type */
//	else
//	{
//		CK_ATTRIBUTE xAttrTemplate =
//		{
//			.pValue = &xKeyType,
//			.type = CKA_KEY_TYPE,
//			.ulValueLen = sizeof( CK_KEY_TYPE )
//		};
//
//		xResult = pxFunctionList->C_GetAttributeValue( xSessionHandle,
//													   xPkHandle,
//													   &xAttrTemplate,
//													   sizeof( xAttrTemplate ) / sizeof( CK_ATTRIBUTE ) );
//	}
//
//	if( xResult == CKR_OK )
//	{
//		xResult = CKR_FUNCTION_FAILED;
//
//		switch( xKeyType )
//		{
//			case CKK_ECDSA:
//				pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) = psa_ecdsa_ctx_alloc();
//				if( pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) != NULL )
//				{
//					xResult = psa_ecdsa_ctx_init( pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx),
//												  pxFunctionList, xSessionHandle, xPkHandle );
//				}
//
//				if( xResult == CKR_OK )
//				{
//					pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_info) = &mbedtls_pkcs11_pk_ecdsa;
//				}
//				else
//				{
//					psa_ecdsa_ctx_free( pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) );
//					pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) = NULL;
//					pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_info) = NULL;
//				}
//				break;
//
//			case CKK_RSA:
//				pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) = p11_rsa_ctx_alloc();
//				if( pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) != NULL )
//				{
//					xResult = p11_rsa_ctx_init( pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx),
//											    pxFunctionList, xSessionHandle, xPkHandle );
//				}
//
//				if( xResult == CKR_OK )
//				{
//					pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_info) = &mbedtls_pkcs11_pk_rsa;
//				}
//				else
//				{
//					p11_rsa_ctx_free( pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) );
//					pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) = NULL;
//					pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_info) = NULL;
//				}
//				break;
//
//			default:
//				pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_ctx) = NULL;
//				pxMbedtlsPkCtx->MBEDTLS_PRIVATE(pk_info) = NULL;
//				break;
//		}
//	}
//
//	return ( xResult == CKR_OK ? 0 : -1 * xResult );
//}
//
//static void * psa_ecdsa_ctx_alloc( void )
//{
//	void * pvCtx = NULL;
//
//	pvCtx = mbedtls_calloc( 1, sizeof( P11EcDsaCtx_t ) );
//
//	if( pvCtx != NULL )
//	{
//		P11EcDsaCtx_t * pxP11EcDsa = ( P11EcDsaCtx_t * ) pvCtx;
//
//		/* Initialize other fields */
//		pxP11EcDsa->xP11PkCtx.pxFunctionList = NULL;
//		pxP11EcDsa->xP11PkCtx.xSessionHandle = CK_INVALID_HANDLE;
//		pxP11EcDsa->xP11PkCtx.xPkHandle = CK_INVALID_HANDLE;
//
//		mbedtls_ecdsa_init( &( pxP11EcDsa->xMbedEcDsaCtx ) );
//	}
//	return pvCtx;
//}
//
//static void psa_ecdsa_ctx_free( void * pvCtx )
//{
//	if( pvCtx != NULL )
//	{
//		P11EcDsaCtx_t * pxP11EcDsa = ( P11EcDsaCtx_t * ) pvCtx;
//
//		mbedtls_ecdsa_free( &( pxP11EcDsa->xMbedEcDsaCtx ) );
//
//		mbedtls_free( pvCtx );
//	}
//}
//
//
//static CK_RV psa_ecdsa_ctx_init( void * pvCtx,
//								 CK_FUNCTION_LIST_PTR pxFunctionList,
//								 CK_SESSION_HANDLE xSessionHandle,
//								 CK_OBJECT_HANDLE xPkHandle )
//{
//	CK_RV xResult = CKR_OK;
//	P11EcDsaCtx_t * pxP11EcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;
//	mbedtls_ecdsa_context * pxMbedEcDsaCtx = NULL;
//
//	configASSERT( pxFunctionList != NULL );
//	configASSERT( xSessionHandle != CK_INVALID_HANDLE );
//	configASSERT( xPkHandle != CK_INVALID_HANDLE );
//
//	if( pxP11EcDsaCtx != NULL )
//	{
//		pxMbedEcDsaCtx = &( pxP11EcDsaCtx->xMbedEcDsaCtx );
//	}
//	else
//	{
//		xResult = CKR_FUNCTION_FAILED;
//	}
//
//	/* Initialize public EC parameter data from attributes */
//
//	CK_ATTRIBUTE pxAttrs[ 2 ] =
//	{
//		{ .type = CKA_EC_PARAMS, .ulValueLen = 0, .pValue = NULL },
//		{ .type = CKA_EC_POINT,  .ulValueLen = 0, .pValue = NULL }
//	};
//
//	/* Determine necessary size */
//	xResult = pxFunctionList->C_GetAttributeValue( xSessionHandle,
//										    xPkHandle,
//											pxAttrs,
//											sizeof( pxAttrs ) / sizeof( CK_ATTRIBUTE ) );
//
//	if( xResult == CKR_OK )
//	{
//		if( pxAttrs[ 0 ].ulValueLen > 0 )
//		{
//			pxAttrs[ 0 ].pValue = pvPortMalloc( pxAttrs[ 0 ].ulValueLen );
//		}
//
//		if( pxAttrs[ 1 ].ulValueLen > 0 )
//		{
//			pxAttrs[ 1 ].pValue = pvPortMalloc( pxAttrs[ 1 ].ulValueLen );
//		}
//
//		xResult = pxFunctionList->C_GetAttributeValue( xSessionHandle,
//												xPkHandle,
//												pxAttrs,
//												sizeof( pxAttrs ) / sizeof( CK_ATTRIBUTE ) );
//	}
//
//	/* Parse EC Group */
//	if( xResult == CKR_OK )
//	{
//		if( mbedtls_ecp_tls_read_group( &( pxMbedEcDsaCtx->MBEDTLS_PRIVATE( grp ) ),
//										pxAttrs[ 0 ].pValue,
//										pxAttrs[ 0 ].ulValueLen ) < 0 )
//		{
//			xResult = CKR_GENERAL_ERROR;
//		}
//	}
//
//	/* Parse EC Point */
//	if( xResult == CKR_OK )
//	{
//		if( mbedtls_ecp_tls_read_point( &( pxMbedEcDsaCtx->MBEDTLS_PRIVATE( grp ) ),
//										&( pxMbedEcDsaCtx->MBEDTLS_PRIVATE( Q ) ),
//										pxAttrs[ 1 ].pValue,
//										pxAttrs[ 1 ].ulValueLen ) < 0 )
//		{
//			xResult = CKR_GENERAL_ERROR;
//		}
//	}
//
//	if( xResult == CKR_OK )
//	{
//		pxP11EcDsaCtx->xP11PkCtx.pxFunctionList = pxFunctionList;
//		pxP11EcDsaCtx->xP11PkCtx.xSessionHandle = xSessionHandle;
//		pxP11EcDsaCtx->xP11PkCtx.xPkHandle = xPkHandle;
//	}
//
//	return xResult;
//}
//
//static int prvASN1WriteBigIntFromOctetStr( unsigned char ** ppucPosition, const unsigned char * pucStart,
//								 	 	   const unsigned char * pucOctetStr, size_t xOctetStrLen )
//{
//	size_t xRequiredLen = 0;
//	int lReturn = 0;
//
//	/* Check if zero byte is needed at beginning */
//	if( pucOctetStr[0] > 0x7F )
//	{
//		xRequiredLen = xOctetStrLen + 1;
//	}
//	else
//	{
//		xRequiredLen = xOctetStrLen;
//	}
//
//	if( &( ( *ppucPosition )[ -xRequiredLen ] ) >= pucStart )
//	{
//		*ppucPosition = &( (*ppucPosition)[ -xOctetStrLen ] );
//
//		/* Copy octet string */
//		( void ) memcpy( *ppucPosition, pucOctetStr, xOctetStrLen );
//
//		/* Prepend additional byte if necessary */
//		if( pucOctetStr[0] > 0x7F )
//		{
//			*ppucPosition = &( (*ppucPosition)[ -1 ] );
//			**ppucPosition = 0;
//		}
//
//		lReturn = mbedtls_asn1_write_len( ppucPosition, pucStart, xRequiredLen );
//
//		if( lReturn > 0 )
//		{
//			xRequiredLen += lReturn;
//			lReturn = mbedtls_asn1_write_tag( ppucPosition, pucStart, MBEDTLS_ASN1_INTEGER );
//		}
//
//		if( lReturn > 0 )
//		{
//			lReturn = lReturn + xRequiredLen;
//		}
//	}
//
//	return lReturn;
//}
//
///*
//* SEQUENCE LENGTH (of entire rest of signature)
//*      INTEGER LENGTH  (of R component)
//*      INTEGER LENGTH  (of S component)
//*/
//static int prvEcdsaSigToASN1InPlace( unsigned char * pucSig, size_t xSigBufferSize, size_t * pxSigLen )
//{
//	unsigned char pucTempBuf[ MBEDTLS_ECDSA_MAX_LEN ] = { 0 };
//	unsigned char * pucPosition = pucTempBuf + sizeof( pucTempBuf );
//
//	size_t xLen = 0;
//	int lReturn = 0;
//	size_t xComponentLen = *pxSigLen / 2;
//
//	configASSERT( pucSig != NULL );
//	configASSERT( pxSigLen != NULL );
//	configASSERT( xSigBufferSize > *pxSigLen );
//
//	/* Write "S" portion VLT */
//	lReturn = prvASN1WriteBigIntFromOctetStr( &pucPosition, pucTempBuf,
//		     	 	 	 	 	 	 	 	  &( pucSig[ xComponentLen ] ), xComponentLen );
//
//	/* Write "R" Portion VLT */
//	if( lReturn > 0 )
//	{
//		xLen += lReturn;
//		lReturn = prvASN1WriteBigIntFromOctetStr( &pucPosition, pucTempBuf,
//				     	 	 	 	 	 	 	  pucSig, xComponentLen );
//	}
//
//	if( lReturn > 0 )
//	{
//		xLen += lReturn;
//		lReturn = mbedtls_asn1_write_len( &pucPosition, pucTempBuf, xLen );
//	}
//
//	if( lReturn > 0 )
//	{
//		xLen += lReturn;
//		lReturn = mbedtls_asn1_write_tag( &pucPosition, pucTempBuf,
//										  MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE );
//	}
//
//	if( lReturn > 0 )
//	{
//		xLen += lReturn;
//	}
//
//	if( lReturn > 0 && xLen <= xSigBufferSize )
//	{
//		( void ) memcpy( pucSig, pucPosition, xLen );
//		*pxSigLen = xLen;
//	}
//	else
//	{
//		lReturn = -1;
//	}
//	return lReturn;
//}
//
//static int psa_ecdsa_sign( void * pvCtx, mbedtls_md_type_t xMdAlg,
//                           const unsigned char * pucHash, size_t xHashLen,
//                           unsigned char * pucSig, size_t xSigBufferSize, size_t * pxSigLen,
//                           int (* plRng)(void *, unsigned char *, size_t),
//                           void * pvRng )
//{
//    CK_RV xResult = CKR_OK;
//    int32_t lFinalResult = 0;
//    P11EcDsaCtx_t * pxEcDsaCtx = NULL;
//    P11PkCtx_t * pxP11Ctx = NULL;
//    unsigned char pucHashCopy[ xHashLen ];
//
//    CK_MECHANISM xMech =
//    {
//    	.mechanism = CKM_ECDSA,
//	    .pParameter = NULL,
//		.ulParameterLen = 0
//    };
//
//    /* Unused parameters. */
//    ( void ) ( xMdAlg );
//    ( void ) ( plRng );
//    ( void ) ( pvRng );
//
//    configASSERT( pucSig != NULL );
//    configASSERT( xSigBufferSize > 0 );
//    configASSERT( pxSigLen != NULL );
//    configASSERT( pucHash != NULL );
//    configASSERT( xHashLen > 0 );
//
//    if( pvCtx != NULL )
//    {
//    	pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;
//    	pxP11Ctx = &( pxEcDsaCtx->xP11PkCtx );
//    }
//    else
//    {
//    	xResult = CKR_FUNCTION_FAILED;
//    }
//
//    if( CKR_OK == xResult )
//    {
//        /* Use the PKCS#11 module to sign. */
//        xResult = pxP11Ctx->pxFunctionList->C_SignInit( pxP11Ctx->xSessionHandle,
//                                                        &xMech,
//														pxP11Ctx->xPkHandle );
//    }
//
//    if( CKR_OK == xResult )
//    {
//    	CK_ULONG ulSigLen = 0;
//
//    	( void ) memcpy( pucHashCopy, pucHash, xHashLen );
//
//    	xResult = pxP11Ctx->pxFunctionList->C_Sign( pxP11Ctx->xSessionHandle,
//    												pucHashCopy, xHashLen,
//                                                    pucSig, &ulSigLen );
//        if( xResult == CKR_OK )
//        {
//        	*pxSigLen = ulSigLen;
//        }
//    }
//
//    if( xResult == CKR_OK )
//    {
//        xResult = prvEcdsaSigToASN1InPlace( pucSig, xSigBufferSize, pxSigLen );
//    }
//
//    if( xResult != CKR_OK )
//    {
//        LogError( "Failed to sign message using PKCS #11 with error code %02X.", xResult );
//        lFinalResult = -1;
//    }
//    else
//    {
//    	lFinalResult = 0;
//    }
//
//    return lFinalResult;
//
//}
//
///* Shim functions */
//static size_t psa_ecdsa_get_bitlen( const void * pvCtx )
//{
//	P11EcDsaCtx_t * pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;
//	configASSERT( mbedtls_ecdsa_info.get_bitlen );
//
//	return mbedtls_ecdsa_info.get_bitlen( &( pxEcDsaCtx->xMbedEcDsaCtx ) );
//}
//
//static int psa_ecdsa_can_do( mbedtls_pk_type_t xType )
//{
//	return( xType == MBEDTLS_PK_ECDSA );
//}
//
//static int psa_ecdsa_verify( void * pvCtx, mbedtls_md_type_t xMdAlg,
//                             const unsigned char * pucHash, size_t xHashLen,
//							 const unsigned char * pucSig, size_t xSigLen )
//{
//	P11EcDsaCtx_t * pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;
//	configASSERT( mbedtls_ecdsa_info.verify_func );
//
//	return mbedtls_ecdsa_info.verify_func( &( pxEcDsaCtx->xMbedEcDsaCtx ),
//										   xMdAlg,
//										   pucHash, xHashLen,
//										   pucSig, xSigLen );
//
//}
//
//static int psa_ecdsa_check_pair( const void * pvPub, const void * pvPrv,
//                                 int (* lFRng)(void *, unsigned char *, size_t),
//                                 void * pvPRng )
//{
//	P11EcDsaCtx_t * pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvPrv;
//	configASSERT( mbedtls_ecdsa_info.check_pair_func );
//
//	return mbedtls_ecdsa_info.check_pair_func( pvPub, &( pxEcDsaCtx->xMbedEcDsaCtx ),
//											   lFRng, pvPRng );
//}
//
//static void psa_ecdsa_debug( const void * pvCtx, mbedtls_pk_debug_item * pxItems )
//{
//	P11EcDsaCtx_t * pxEcDsaCtx = ( P11EcDsaCtx_t * ) pvCtx;
//	configASSERT( mbedtls_ecdsa_info.debug_func );
//
//    return mbedtls_ecdsa_info.debug_func( &( pxEcDsaCtx->xMbedEcDsaCtx ), pxItems );
//}

#endif /* MBEDTLS_TRANSPORT_PSA */
