#include <stddef.h>

/*int lParseECPoint(  ) */

static int lParseCurveSequence( mbedtls_mpi * pxFieldElementA,
                                mbedtls_mpi * pxFieldElementB,
                                unsigned char * pucIterator,
                                size_t uxLength )
{
    int lResult = -1;
    unsigned char * pucEnd = NULL;
    size_t uxFieldLen = 0;

    configASSERT( pxFieldElementA != NULL );
    configASSERT( pxFieldElementB != NULL );
    configASSERT( pucIterator != NULL );
    configASSERT( uxLength != 0 );

    pucEnd = &( pucIterator[ uxLength ] );

    /* Process FieldElementA */
    lResult = mbedtls_asn1_get_tag( &pucIterator, pucEnd, &uxFieldLen, MBEDTLS_ASN1_OCTET_STRING );

    if( lResult == 0 )
    {
        lResult = mbedtls_mpi_read_binary( pxFieldElementA, pucIterator, uxFieldLen );

        if( lResult == 0 )
        {
            pucIterator = &( pucIterator[ uxFieldLen ] );
        }
    }

    if( lResult == 0 )
    {
        lResult = mbedtls_asn1_get_tag( &pucIterator, pucEnd, &uxFieldLen, MBEDTLS_ASN1_OCTET_STRING );
    }

    /* Process FieldElementB */
    if( lResult == 0 )
    {
        lResult = mbedtls_mpi_read_binary( pxFieldElementB, pucIterator, uxFieldLen );

        if( lResult == 0 )
        {
            pucIterator = &( pucIterator[ uxFieldLen ] );
        }
    }

    /* Ignore optional field */
    if( lResult == 0 )
    {
        lResult = mbedtls_asn1_get_tag( &pucIterator, pucEnd, &uxFieldLen, MBEDTLS_ASN1_OCTET_STRING );

        if( lResult == 0 )
        {
            pucIterator = &( pucIterator[ uxFieldLen ] );
        }
        else
        {
            lResult = 0;
        }
    }

    if( pucIterator != pucEnd )
    {
        lResult = MBEDTLS_ERR_ASN1_LENGTH_MISMATCH;
    }

    return lResult;
}

static int lParseFieldIDSequence( mbedtls_mpi * pxPrimeModulus,
                                  size_t * puxPrimeModulusBits,
                                  unsigned char * pucIterator,
                                  size_t uxLength )
{
    int lResult = -1;
    const unsigned char * pucEnd = NULL;
    size_t uxFieldLen = 0;

    configASSERT( pxPrimeModulus != NULL );
    configASSERT( puxPrimeModulusBits != NULL );
    configASSERT( pucIterator != NULL );
    configASSERT( uxLength > 0 );

    pucEnd = &( pucIterator[ uxLength ] );

    /* Get OID */
    lResult = mbedtls_asn1_get_tag( &pucIterator, pucEnd,
                                    &uxFieldLen, MBEDTLS_ASN1_OID );

    if( ( lResult == 0 ) &&
        ( ( MBEDTLS_OID_SIZE( MBEDTLS_OID_ANSI_X9_62_PRIME_FIELD ) != uxFieldLen ) ||
          ( memcmp( pucIterator, MBEDTLS_OID_ANSI_X9_62_PRIME_FIELD, uxFieldLen ) != 0 ) ) )
    {
        lResult = MBEDTLS_ERR_ASN1_UNEXPECTED_TAG;
    }

    if( lResult == 0 )
    {
        lResult = mbedtls_asn1_get_mpi( &pucIterator, pucEnd, pxPrimeModulus );
    }

    if( lResult == 0 )
    {
        /* Increment iterator */
        pucIterator = &( pucIterator[ uxFieldLen ] );

        *puxPrimeModulusBits = mbedtls_mpi_bitlen( pxPrimeModulus );
    }

    return lResult;
}


static int lParseGroupFromSpecifiedECDomain( mbedtls_ecp_group * pxEcGroup,
                                             mbedtls_asn1_buf * pxAsn1Buffer )
{
    int lReturn = -1;
    unsigned char * pucIterator = NULL;
    const unsigned char * pucEnd = NULL;
    size_t uxFieldLen = 0;
    int lVersion = 0;

    configASSERT( pxEcGroup != NULL );
    configASSERT( pxAsn1Buffer != NULL );
    configASSERT( pxAsn1Buffer->MBEDTLS_PRIVATE( p ) != NULL );
    configASSERT( pxAsn1Buffer->MBEDTLS_PRIVATE( len ) > 0 );

    pucIterator = pxAsn1Buffer->MBEDTLS_PRIVATE( p );
    pucEnd = &( pucIterator[ pxAsn1Buffer->MBEDTLS_PRIVATE( len ) ] );

    /* Parse SpecifiedECDomainVersion */
    lReturn = mbedtls_asn1_get_int( pucIterator, pucEnd, &lVersion );

    /* Validate version */
    if( ( lReturn == 0 ) &&
        ( ( lVersion < 1 ) ||
          ( lVersion > 3 ) ) )
    {
        lReturn = MBEDTLS_ERR_ASN1_INVALID_DATA;
    }

    if( lReturn == 0 )
    {
        mbedtls_asn1_buf xFieldBuffer;
        xFieldBuffer.MBEDTLS_PRIVATE( tag ) = MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE;

        /* Process fieldID sequence start tag */
        lResult = mbedtls_asn1_get_tag( &pucIterator, &pucEnd,
                                        &( xFieldBuffer.MBEDTLS_PRIVATE( len ) ),
                                        xFieldBuffer.MBEDTLS_PRIVATE( tag ) );

        if( lResult == 0 )
        {
            xFieldBuffer.MBEDTLS_PRIVATE( p ) = pucIterator;

            lResult = lParseFieldIDSequence( &( pxEcGroup->P ),
                                             &( pxEcGroup->pbits ),
                                             &xFieldBuffer );
        }

        /* Advance iterator */
        pucIterator = &( pucIterator[ xFieldBuffer.MBEDTLS_PRIVATE( len ) ] );
    }

    /* Parse curve */
    if( lReturn == 0 )
    {
        mbedtls_asn1_buf xCurveBuffer;
        xCurveBuffer.MBEDTLS_PRIVATE( tag ) = MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE;

        lResult = mbedtls_asn1_get_tag( &pucIterator, &pucEnd,
                                        &( xFieldBuffer.MBEDTLS_PRIVATE( len ) ),
                                        xFieldBuffer.MBEDTLS_PRIVATE( tag ) );

        if( lResult == 0 )
        {
            xCurveBuffer.MBEDTLS_PRIVATE( p ) = pucIterator;

            lResult = lParseCurveSequence( &( pxEcGroup->A ),
                                           &( pxEcGroup->B ),
                                           &xCurveBuffer );
        }
    }

    /* Parse base */

    /* Parse order */
}

int lParseGroupFromECParameters( mbedtls_ecp_group * pxEcGroup,
                                 const unsigned char * pucEcParameters,
                                 size_t uxLength )
{
    int lResult = -1;
    unsigned char * pucIterator = pucEcParameters;
    mbedtls_asn1_buf xAsn1Buffer = { 0 };
    mbedtls_ecp_group_id xEcGroupId = MBEDTLS_ECP_DP_NONE;

    if( ( pucEcParameters != NULL ) &&
        ( uxLength > 0 ) )
    {
        /* Tag is always the first byte */
        xAsn1Buffer.MBEDTLS_PRIVATE( tag ) = *pucEcParameters;

        /* Set iterator to beginning of buffer */
        pucIterator = pucEcParameters;

        /* Update pucIterator and length if tag is found. */
        lResult = mbedtls_asn1_get_tag( &pucIterator, &( pucIterator[ uxLength ] ),
                                        &( xAsn1Buffer.MBEDTLS_PRIVATE( len ) ),
                                        xAsn1Buffer.MBEDTLS_PRIVATE( tag ) );

        /* Check for improper length */
        if( ( lResult == 0 ) &&
            ( &( pucEcParameters[ uxLength ] ) != &( pucIterator[ xAsn1Buffer.MBEDTLS_PRIVATE( len ) ] ) ) )
        {
            lResult = MBEDTLS_ERR_ASN1_INVALID_LENGTH;
        }
    }

    if( lResult == 0 )
    {
        xAsn1Buffer.MBEDTLS_PRIVATE( p ) = pucIterator;

        /* Handle namedCurve case */
        if( xAsn1Buffer.MBEDTLS_PRIVATE( tag ) == MBEDTLS_ASN1_OID )
        {
            lResult = mbedtls_oid_get_ec_grp( &xAsn1Buffer, &xEcGroupId );
        }
        /* Handle specifiedCurve case */
        else if( xAsn1Buffer.MBEDTLS_PRIVATE( tag ) == ( MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE ) )
        {
        }
        /* Unexpected tag */
        else
        {
            lResult = MBEDTLS_ERR_ASN1_UNEXPECTED_TAG;
        }
    }

    return lResult;
}
