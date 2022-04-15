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
#include <string.h>

#include "ota.h"
#include "psa/crypto.h"
#include "mbedtls/pk.h"
#include "ota_config.h"


/**
 * @brief The handle for the code signature validation stored in PSA crypto.
 * Handle is used by the OTA PAL to validate the signature.
 */
psa_key_handle_t xOTACodeVerifyKeyHandle = ( psa_key_handle_t ) PSA_KEY_ID_INIT;
static uint8_t publicKeyASN[ 2048 ] = { 0 };

/**
 * @brief Parses the input public key in PEM or DER format and outputs the SubjectPublicKey in ASN.1 format.
 *
 */
static int prvParsePublicKey( const uint8_t * pubkey,
		                      size_t pubkeyLength,
							  uint8_t * pSubPubkeyBuffer,
							  size_t subPubkeyBufferLength,
							  uint8_t ** pSubPubKey,
							  size_t * pSubPubkeyLength )
{
	mbedtls_pk_context pk;
	int pkRet;
	uint8_t * pBufferPtr = ( pSubPubkeyBuffer + subPubkeyBufferLength );

	mbedtls_pk_init(&pk);

	/* Parse the public key. */
	pkRet = mbedtls_pk_parse_public_key(&pk, pubkey, pubkeyLength );
	if(pkRet < 0)
	{
		LogError( "Failed parsing PEM public key, error = %d", pkRet );
	}
	else
	{
		pkRet = mbedtls_pk_write_pubkey( &( pBufferPtr ), pSubPubkeyBuffer, &pk );
		if( pkRet > 0 )
		{
			( * pSubPubKey ) = pBufferPtr;
			( * pSubPubkeyLength ) = pkRet;
		}
	}

	mbedtls_pk_free( &pk );

	return pkRet;

}


OtaPalStatus_t otaPal_Init( void )
{
	OtaPalStatus_t status = OtaPalSuccess;
	psa_status_t psaStatus;
	int pkStatus = 0;
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	size_t pubKeyLength = 0;
	uint8_t * pubKey = NULL;

	psa_set_key_lifetime( &attributes, PSA_KEY_LIFETIME_VOLATILE );
	psa_set_key_usage_flags(&attributes, 0);
	psa_set_key_algorithm(&attributes, 0);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attributes, 256);

	pkStatus = prvParsePublicKey( ( uint8_t * ) configOTA_CODE_VERIFY_PUBLIC_KEY,
			                        sizeof( configOTA_CODE_VERIFY_PUBLIC_KEY ),
									publicKeyASN,
									sizeof( publicKeyASN ),
								    &pubKey,
									&pubKeyLength );

	if( pkStatus < 0 )
	{
		LogError( "Failed to parse the public key with error = %d", pkStatus );
		status = OTA_PAL_COMBINE_ERR( OtaPalBadSignerCert, pkStatus );
	}

	if( status == OtaPalSuccess )
	{

		psaStatus = psa_import_key( &attributes,
				pubKey,
				pubKeyLength,
				( psa_key_id_t * ) ( &xOTACodeVerifyKeyHandle ) );
		if( psaStatus != PSA_SUCCESS )
		{
			status = OTA_PAL_COMBINE_ERR( OtaPalBadSignerCert, psaStatus );
		}
	}

	return status;

}



