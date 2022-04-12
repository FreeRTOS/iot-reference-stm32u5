#include "mbedtls/entropy.h"
#include "mbedtls/base64.h"
#include "cli.h"
#include "cli_prv.h"

static void prvRngTestCommand( ConsoleIO_t * const pxCIO,
                               uint32_t ulArgc,
                               char * ppcArgv[] );

const CLI_Command_Definition_t xCommandDef_rngtest =
{
    "rngtest",
    "rngtest <number of bytes>\r\n"
    "    Read the specified number of bytes from the rng and output them base64 encoded.\r\n\n",
    prvRngTestCommand
};

static void prvRngTestCommand( ConsoleIO_t * const pxCIO,
                               uint32_t ulArgc,
                               char * ppcArgv[] )
{
    size_t uxNumRandomBytes = 2500;
    size_t uxBytesWritten = 0;

    mbedtls_entropy_context xEntropyCtx;
    unsigned char pcBuffer[ 2 * MBEDTLS_ENTROPY_BLOCK_SIZE ];
    unsigned char pucEntropyBuffer[ MBEDTLS_ENTROPY_BLOCK_SIZE ] = { 0 };

    if( ulArgc > 1 )
    {
        char * pcArg = ppcArgv[ 1 ];
        uxNumRandomBytes = ( size_t ) strtoul( pcArg, NULL, 0 );
    }

    mbedtls_entropy_init( &xEntropyCtx );

    while( uxBytesWritten < uxNumRandomBytes )
    {
        size_t uxCharsWrittenThisIter = 0;
        int lError = 0;


        lError = mbedtls_entropy_func( &xEntropyCtx, pucEntropyBuffer, MBEDTLS_ENTROPY_BLOCK_SIZE );

        if( lError != 0 )
        {
            pxCIO->print( "Error: mbedtls_entropy_func call failed." );
            break;
        }
        else
        {
            lError = mbedtls_base64_encode( pcBuffer, 2 * MBEDTLS_ENTROPY_BLOCK_SIZE,
                                            &uxCharsWrittenThisIter,
                                            pucEntropyBuffer, MBEDTLS_ENTROPY_BLOCK_SIZE );

            if( lError == 0 )
            {
                pxCIO->write( pcBuffer, uxCharsWrittenThisIter );
                uxBytesWritten += MBEDTLS_ENTROPY_BLOCK_SIZE;
            }
            else
            {
                pxCIO->print( "Error: mbedtls_base64_encode call failed." );
                break;
            }
        }
    }

    pxCIO->print( "\r\nDONE\r\n" );

    mbedtls_entropy_free( &xEntropyCtx );
}
