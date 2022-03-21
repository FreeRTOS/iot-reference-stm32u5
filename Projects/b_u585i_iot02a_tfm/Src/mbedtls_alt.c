#include "entropy_poll.h"
#include "psa/crypto.h"

int mbedtls_hardware_poll( void * data,
                           unsigned char * output,
                           size_t len,
                           size_t * olen )
{
    ( void ) data;
    int lReturn = psa_generate_random( output, len );

    if( lReturn == PSA_SUCCESS )
    {
        *olen = len;
    }
    else
    {
        *olen = 0;
    }

    return lReturn;
}
