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

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "task.h"

#include "cli.h"
#include "cli_prv.h"
#include "logging.h"

#include "kvstore.h"

#include <string.h>
#include <stdlib.h>

#define MODE_ARG_IDX     1
#define KEY_ARG_IDX      2
#define VALUE_ARG_IDX    3

/* Local static functions */
static void vSubCommand_CommitConfig( ConsoleIO_t * pxCIO );
static void vSubCommand_GetConfig( ConsoleIO_t * pxCIO,
                                   const char * const pcKey );
static void vSubCommand_GetConfigAll( ConsoleIO_t * pxCIO );
static void vSubCommand_SetConfig( ConsoleIO_t * pxCIO,
                                   uint32_t ulArgc,
                                   char * ppcArgv[] );
static void vCommand_Configure( ConsoleIO_t * pxCIO,
                                uint32_t ulArgc,
                                char * ppcArgv[] );

const CLI_Command_Definition_t xCommandDef_conf =
{
    .pcCommand            = "conf",
    .pcHelpString         =
        "conf:\r\n"
        "    Get/ Set/ Commit runtime configuration values\r\n"
        "    Usage:\r\n"
        "    conf get\r\n"
        "        Outputs the value of all runtime config options supported by the system.\r\n\n"
        "    conf get <key>\r\n"
        "        Outputs the current value of a given runtime config item.\r\n\n"
        "    conf set <key> <value>\r\n"
        "        Set the value of a given runtime config item. This change is staged\r\n"
        "        in volatile memory until a commit operation occurs.\r\n\n"
        "    conf commit\r\n"
        "        Commit staged config changes to nonvolatile memory.\r\n\n",
    .pxCommandInterpreter = vCommand_Configure
};

static void vSubCommand_CommitConfig( ConsoleIO_t * pxCIO )
{
    BaseType_t xResult = KVStore_xCommitChanges();

    if( xResult == pdTRUE )
    {
        pxCIO->print( "Configuration saved to NVM.\r\n" );
    }
    else
    {
        pxCIO->print( "Error: Could not save configuration to NVM.\r\n" );
    }
}

static void vSubCommand_GetConfig( ConsoleIO_t * pxCIO,
                                   const char * const pcKey )
{
    KVStoreKey_t xKey = kvStringToKey( pcKey );
    KVStoreValueType_t xKvType = KVStore_getType( xKey );

    int32_t lResponseLen = 0;

    switch( xKvType )
    {
        case KV_TYPE_BASE_T:
           {
               BaseType_t xValue = KVStore_getBase( xKey, NULL );
               lResponseLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, "%s=%ld\r\n",
                                        pcKey, xValue );
               break;
           }

        case KV_TYPE_UBASE_T:
           {
               UBaseType_t xValue = KVStore_getUBase( xKey, NULL );
               lResponseLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, "%s=%lu\r\n",
                                        pcKey, xValue );
               break;
           }

        case KV_TYPE_INT32:
           {
               int32_t lValue = KVStore_getInt32( xKey, NULL );
               lResponseLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, "%s=%ld\r\n",
                                        pcKey, lValue );
               break;
           }

        case KV_TYPE_UINT32:
           {
               uint32_t ulValue = KVStore_getUInt32( xKey, NULL );
               lResponseLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, "%s=%lu\r\n",
                                        pcKey, ulValue );
               break;
           }

        case KV_TYPE_STRING:
        case KV_TYPE_BLOB:
           {
               char * pcWorkPtr = pcCliScratchBuffer;
               pcWorkPtr = stpncpy( pcWorkPtr, pcKey, CLI_OUTPUT_SCRATCH_BUF_LEN );

               lResponseLen = ( ( uintptr_t ) pcWorkPtr - ( uintptr_t ) pcCliScratchBuffer );

               if( lResponseLen < CLI_OUTPUT_SCRATCH_BUF_LEN )
               {
                   *pcWorkPtr = '=';
                   pcWorkPtr++;
                   lResponseLen++;
                   *pcWorkPtr = '"';
                   pcWorkPtr++;
                   lResponseLen++;
               }

               if( lResponseLen < CLI_OUTPUT_SCRATCH_BUF_LEN )
               {
                   lResponseLen += KVStore_getString( xKey, pcWorkPtr,
                                                      CLI_OUTPUT_SCRATCH_BUF_LEN - lResponseLen );
                   pcWorkPtr = &( pcCliScratchBuffer[ lResponseLen ] );
               }

               if( ( lResponseLen + 3 ) > CLI_OUTPUT_SCRATCH_BUF_LEN )
               {
                   pcWorkPtr = &( pcCliScratchBuffer[ CLI_OUTPUT_SCRATCH_BUF_LEN - 3 ] );
                   lResponseLen = CLI_OUTPUT_SCRATCH_BUF_LEN - 3;
               }

               *pcWorkPtr = '"';
               pcWorkPtr++;
               *pcWorkPtr = '\r';
               pcWorkPtr++;
               *pcWorkPtr = '\n';
               pcWorkPtr++;
               lResponseLen += 3;
               break;
           }

        case KV_TYPE_LAST:
        case KV_TYPE_NONE:
        default:
            lResponseLen = 0;
            break;
    }

    if( ( xKey == CS_NUM_KEYS ) ||
        ( xKvType == KV_TYPE_LAST ) )
    {
    }

    /* Ensure null terminated */
    if( lResponseLen < CLI_OUTPUT_SCRATCH_BUF_LEN )
    {
        pcCliScratchBuffer[ lResponseLen ] = '\0';
    }
    else
    {
        pcCliScratchBuffer[ CLI_OUTPUT_SCRATCH_BUF_LEN - 1 ] = '\0';
    }

    if( lResponseLen > 0 )
    {
        /* Print call expects a null terminated string */
        pxCIO->print( pcCliScratchBuffer );
    }
    else
    {
        if( ( xKey == CS_NUM_KEYS ) ||
            ( xKvType == KV_TYPE_NONE ) )
        {
            lResponseLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                     "Error: key: %s was not recognized.\r\n",
                                     pcKey );
        }
        else
        {
            lResponseLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                     "Error: An unknown error occurred.\r\n" );
        }

        pxCIO->print( pcCliScratchBuffer );
    }
}

static void vSubCommand_GetConfigAll( ConsoleIO_t * pxCIO )
{
    for( KVStoreKey_t key = 0; key < CS_NUM_KEYS; key++ )
    {
        vSubCommand_GetConfig( pxCIO, kvStoreKeyMap[ key ] );
    }
}

static void vSubCommand_SetConfig( ConsoleIO_t * pxCIO,
                                   uint32_t ulArgc,
                                   char * ppcArgv[] )
{
    const char * pcKey = ppcArgv[ 2 ];

    const char * pcValue = "";

    if( ulArgc > VALUE_ARG_IDX )
    {
        pcValue = ppcArgv[ 3 ];
    }

    BaseType_t xParseResult = pdFALSE;
    int lCharsPrinted = 0;

    if( pcKey != NULL )
    {
        KVStoreKey_t xKey = kvStringToKey( pcKey );
        KVStoreValueType_t xKvType = KVStore_getType( xKey );
        char * pcEndPtr = NULL;

        switch( xKvType )
        {
            case KV_TYPE_BASE_T:
               {
                   BaseType_t xValue = strtol( pcValue, &pcEndPtr, 10 );

                   if( ( pcEndPtr != pcValue ) || ( xValue == 0 ) )
                   {
                       ( void ) KVStore_setBase( xKey, xValue );
                       xParseResult = pdTRUE;
                       lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                                 "%s=%ld\r\n",
                                                 pcKey, xValue );
                   }

                   break;
               }

            case KV_TYPE_INT32:
               {
                   int32_t lValue = strtol( pcValue, &pcEndPtr, 10 );

                   if( ( pcEndPtr != pcValue ) || ( lValue == 0 ) )
                   {
                       ( void ) KVStore_setInt32( xKey, lValue );
                       xParseResult = pdTRUE;
                       lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                                 "%s=%ld\r\n",
                                                 pcKey, lValue );
                   }

                   break;
               }

            case KV_TYPE_UBASE_T:
               {
                   UBaseType_t uxValue = strtoul( pcValue, &pcEndPtr, 10 );

                   if( ( pcEndPtr != pcValue ) || ( uxValue == 0 ) )
                   {
                       ( void ) KVStore_setUBase( xKey, uxValue );
                       xParseResult = pdTRUE;
                       lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                                 "%s=%lu\r\n",
                                                 pcKey, uxValue );
                   }

                   break;
               }

            case KV_TYPE_UINT32:
               {
                   uint32_t ulValue = strtoul( pcValue, &pcEndPtr, 10 );

                   if( ( pcEndPtr != pcValue ) || ( ulValue == 0 ) )
                   {
                       ( void ) KVStore_setUInt32( xKey, ulValue );
                       xParseResult = pdTRUE;
                       lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                                 "%s=%lu\r\n",
                                                 pcKey, ulValue );
                   }

                   break;
               }

            case KV_TYPE_STRING:
                xParseResult = KVStore_setString( xKey, pcValue );
                lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                          "%s=\"%s\"\r\n",
                                          pcKey, pcValue );
                break;

            case KV_TYPE_BLOB:
                xParseResult = KVStore_setBlob( xKey, strlen( pcValue ), pcValue );
                lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                          "%s=\"%s\"\r\n",
                                          pcKey, pcValue );
                break;

            default:
                break;
        }

        if( xParseResult == pdFALSE )
        {
            if( xKey == CS_NUM_KEYS )
            {
                lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                          "Error: key: %s was not recognized.\r\n",
                                          pcKey );
            }
            else
            {
                lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                                          "Error: value: %s is not valid for key: %s\r\n",
                                          pcValue, pcKey );
            }
        }

        /* Truncate output */
        if( lCharsPrinted > CLI_OUTPUT_SCRATCH_BUF_LEN )
        {
            lCharsPrinted = CLI_OUTPUT_SCRATCH_BUF_LEN;
        }

        if( lCharsPrinted > 0 )
        {
            pxCIO->write( pcCliScratchBuffer, ( size_t ) lCharsPrinted );
        }
    }
    else
    {
        pxCIO->print( "An unknown error occurred." );
    }
}



/*
 * CLI format:
 * Argc   1    2      3     4
 * Idx    0    1      2     3
 *      conf get    <key>
 *      conf set    <key> <value>
 *      conf commit
 */
static void vCommand_Configure( ConsoleIO_t * pxCIO,
                                uint32_t ulArgc,
                                char * ppcArgv[] )
{
    const char * pcMode = NULL;

    BaseType_t xSuccess = pdFALSE;

    if( ulArgc > MODE_ARG_IDX )
    {
        pcMode = ppcArgv[ MODE_ARG_IDX ];

        if( 0 == strcmp( "get", pcMode ) )
        {
            /* If a second argument was provided, get a specific config item */
            if( ulArgc > KEY_ARG_IDX )
            {
                vSubCommand_GetConfig( pxCIO, ppcArgv[ KEY_ARG_IDX ] );
            }
            /* Otherwise list all config items */
            else
            {
                vSubCommand_GetConfigAll( pxCIO );
            }

            xSuccess = pdTRUE;
        }
        else if( 0 == strcmp( "set", pcMode ) )
        {
            vSubCommand_SetConfig( pxCIO, ulArgc, ppcArgv );
            xSuccess = pdTRUE;
        }
        else if( 0 == strcmp( "commit", pcMode ) )
        {
            vSubCommand_CommitConfig( pxCIO );
            xSuccess = pdTRUE;
        }
        else
        {
            xSuccess = pdFALSE;
        }
    }

    if( xSuccess == pdFALSE )
    {
        pxCIO->print( xCommandDef_conf.pcHelpString );
    }
}
