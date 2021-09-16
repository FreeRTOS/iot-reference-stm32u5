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
#include "FreeRTOS_CLI.h"
#include "task.h"

#include "cli.h"
#include "logging.h"

#include "kvstore.h"

#include <string.h>
#include <stdlib.h>

static void vSubCommand_CommitConfig( ConsoleIO_t * pxConsoleIO )
{
    BaseType_t xResult = KVStore_xCommitChanges();

    if( xResult == pdTRUE )
    {
        pxConsoleIO->print( "Configuration saved to NVM." );
    }
    else
    {
        pxConsoleIO->print( "Error: Could not save configuration to NVM." );
    }
}

static void vSubCommand_GetConfig( ConsoleIO_t * pxConsoleIO, char * pcKey )
{
    KVStoreKey_t xKey = kvStringToKey( pcKey );
    KVStoreValueType_t xKvType = KVStore_getType( xKey );

    int32_t lResponseLen = 0;

    switch( xKvType )
    {
    case KV_TYPE_BASE_T:
    {
        BaseType_t xValue = KVStore_getBase( xKey, NULL );
        lResponseLen = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN, "%s=%ld\r\n", pcKey, xValue );
        break;
    }
    case KV_TYPE_UBASE_T:
    {
        UBaseType_t xValue = KVStore_getUBase( xKey, NULL );
        lResponseLen = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN, "%s=%lu\r\n", pcKey, xValue );
        break;
    }
    case KV_TYPE_INT32:
    {
        int32_t lValue = KVStore_getInt32( xKey, NULL );
        lResponseLen = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN, "%s=%ld\r\n", pcKey, lValue );
        break;
    }
    case KV_TYPE_UINT32:
    {
        uint32_t ulValue = KVStore_getUInt32( xKey, NULL );
        lResponseLen = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN, "%s=%lu\r\n", pcKey, ulValue );
        break;
    }
    case KV_TYPE_STRING:
    case KV_TYPE_BLOB:
    {
        char * pcWorkPtr = pcCliScratchBuffer;
        pcWorkPtr = stpncpy( pcWorkPtr, pcKey, CLI_SCRATCH_BUF_LEN );

        lResponseLen = ( ( uintptr_t ) pcWorkPtr - ( uintptr_t ) pcCliScratchBuffer );

        if( lResponseLen < CLI_SCRATCH_BUF_LEN )
        {
            *pcWorkPtr = '=';
            pcWorkPtr++;
            lResponseLen++;
            *pcWorkPtr = '"';
            pcWorkPtr++;
            lResponseLen++;
        }

        if( lResponseLen < CLI_SCRATCH_BUF_LEN )
        {
            lResponseLen += KVStore_getString( xKey, pcWorkPtr, CLI_SCRATCH_BUF_LEN - lResponseLen );
            pcWorkPtr = &( pcCliScratchBuffer[ lResponseLen ] );
        }

        if( ( lResponseLen + 3 ) > CLI_SCRATCH_BUF_LEN )
        {
            pcWorkPtr = &( pcCliScratchBuffer[ CLI_SCRATCH_BUF_LEN - 3 ] );
            lResponseLen = CLI_SCRATCH_BUF_LEN - 3;
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

    if( xKey == CS_NUM_KEYS ||
        xKvType == KV_TYPE_LAST )
    {

    }

    /* Ensure null terminated */
    if( lResponseLen < CLI_SCRATCH_BUF_LEN )
    {
        pcCliScratchBuffer[ lResponseLen ] = '\0';
    }
    else
    {
        pcCliScratchBuffer[ CLI_SCRATCH_BUF_LEN - 1 ] = '\0';
    }

    if( lResponseLen > 0 )
    {
        /* Print call expects a null terminated string */
        pxConsoleIO->print( pcCliScratchBuffer );
    }
    else
    {
        if( xKey == CS_NUM_KEYS ||
            xKvType == KV_TYPE_NONE )
        {
            lResponseLen = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                      "Error: key: %s was not recognized.\r\n", pcKey );
        }
        else
        {
            lResponseLen = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                      "Error: An unknown error occurred.\r\n" );
        }
        pxConsoleIO->print( pcCliScratchBuffer );
    }
}


static void vSubCommand_GetConfigAll( ConsoleIO_t * pxConsoleIO )
{
    for( KVStoreKey_t key = 0; key < CS_NUM_KEYS; key++ )
    {
        vSubCommand_GetConfig( pxConsoleIO, kvStoreKeyMap[ key ] );
    }
}


static void vSubCommand_SetConfig( ConsoleIO_t * pxConsoleIO, char * pcKey, char * pcValue )
{
    KVStoreKey_t xKey = kvStringToKey( pcKey );
    KVStoreValueType_t xKvType = KVStore_getType( xKey );
    char * pcEndPtr = NULL;

    BaseType_t xParseResult = pdFALSE;
    int lCharsPrinted = 0;

    switch( xKvType )
    {
    case KV_TYPE_BASE_T:
    {
        BaseType_t xValue = strtol( pcValue, &pcEndPtr, 10 );
        if( pcEndPtr != pcValue )
        {
            ( void ) KVStore_setBase( xKey, xValue );
            xParseResult = pdTRUE;
            lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                      "%s=%ld\r\n", pcKey, xValue );
        }
        break;
    }
    case KV_TYPE_INT32:
    {
        int32_t lValue = strtol( pcValue, &pcEndPtr, 10 );
        if( pcEndPtr != pcValue )
        {
            ( void ) KVStore_setInt32( xKey, lValue );
            xParseResult = pdTRUE;
            lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                      "%s=%ld\r\n", pcKey, lValue );
        }
        break;
    }
    case KV_TYPE_UBASE_T:
    {
        UBaseType_t uxValue = strtoul( pcValue, &pcEndPtr, 10 );
        if( pcEndPtr != pcValue )
        {
            ( void ) KVStore_setUBase( xKey, uxValue );
            xParseResult = pdTRUE;
            lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                      "%s=%lu\r\n", pcKey, uxValue );
        }
        break;
    }
    case KV_TYPE_UINT32:
    {
        uint32_t ulValue = strtoul( pcValue, &pcEndPtr, 10 );
        if( pcEndPtr != pcValue )
        {
            ( void ) KVStore_setUBase( xKey, ulValue );
            xParseResult = pdTRUE;
            lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                      "%s=%lu\r\n", pcKey, ulValue );
        }
        break;
    }
    case KV_TYPE_STRING:
    {
        xParseResult = KVStore_setString( xKey, strlen( pcValue ), pcValue );
        lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                  "%s=%s\r\n", pcKey, pcValue );
        break;
    }
    case KV_TYPE_BLOB:
    {
        xParseResult = KVStore_setBlob( xKey, strlen( pcValue ), pcValue );
        lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                  "%s=%s\r\n", pcKey, pcValue );
        break;
    }
    default:
        break;
    }

    if( xParseResult == pdFALSE )
    {
        if( xKey == CS_NUM_KEYS )
        {
            lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                      "Error: key: %s was not recognized.\r\n", pcKey );
        }
        else
        {
            lCharsPrinted = snprintf( pcCliScratchBuffer, CLI_SCRATCH_BUF_LEN,
                                      "Error: value: %s is not valid for key: %s\r\n", pcValue, pcKey );
        }
    }

    /* Ensure null terminated */
    if( lCharsPrinted < CLI_SCRATCH_BUF_LEN )
    {
        pcCliScratchBuffer[ lCharsPrinted ] = '\0';
    }
    else
    {
        pcCliScratchBuffer[ CLI_SCRATCH_BUF_LEN - 1 ] = '\0';
    }

    /* Print call expects a null terminated string */
    pxConsoleIO->print( pcCliScratchBuffer );
}

/* Assumes FS was already mounted */
static BaseType_t xCommand_Configure( ConsoleIO_t * pxConsoleIO, const char *pcCommandString )
{
    /* Fetch mode */
    BaseType_t xModeRefLength = 0;
    const char * pcModeRef = FreeRTOS_CLIGetParameter( pcCommandString, 1, &xModeRefLength );
    char pcMode[xModeRefLength + 1];
    memcpy( pcMode, pcModeRef, xModeRefLength);
    pcMode[xModeRefLength] = '\0';

    /* Fetch key */
    BaseType_t xKeyRefLength = 0;
    const char * pcKeyRef = FreeRTOS_CLIGetParameter( pcCommandString, 2, &xKeyRefLength );
    char pcKey[xKeyRefLength + 1];
    if( pcKeyRef != NULL )
    {
        ( void ) memcpy( pcKey, pcKeyRef, xKeyRefLength );
    }

    pcKey[xKeyRefLength] = '\0';

    if( 0 == strcmp( "get", pcMode ) )
    {
        if( xKeyRefLength > 0 )
        {
            vSubCommand_GetConfig( pxConsoleIO, pcKey );
        }
        else
        {
            vSubCommand_GetConfigAll( pxConsoleIO );
        }
    }
    else if( 0 == strcmp( "set", pcMode ) && xKeyRefLength > 1 )
    {
        /* Fetch value */
        BaseType_t xValueRefLength = 0;
        const char * pcValueRef = FreeRTOS_CLIGetParameter( pcCommandString, 3, &xValueRefLength );
        char pcValue[xValueRefLength + 1];
        memcpy( pcValue, pcValueRef, xValueRefLength );
        pcValue[xValueRefLength] = '\0';

        vSubCommand_SetConfig( pxConsoleIO, pcKey, pcValue );
    }
    else if( 0 == strcmp( "commit", pcMode ) )
    {
        vSubCommand_CommitConfig( pxConsoleIO );
    }
    else
    {
        const char * confErrStr = "Usage:\r\n conf get <key>\r\n conf set <key> <value>\r\n";
        pxConsoleIO->write( confErrStr, strlen(confErrStr) );
    }

    return pdFALSE;
}

const CLI_Command_Definition_t xCommandDef_conf =
{
        .pcCommand = "conf",
        .pcHelpString = "conf\r\n"
                "    Get/ Set/ Commit runtime configuration values\r\n"
                "    Usage:\r\n"
                "        conf get\r\n"
                "            Outputs the value of all runtime config options supported by the system.\r\n\n"
                "        conf get <key>\r\n"
                "            Outputs the current value of a given runtime config item.\r\n\n"
                "        conf set <key> <value>\r\n"
                "            Set the value of a given runtime config item. This change is staged\r\n\n"
                "            in volatile memory until a commit operation occurs.\r\n\n"
                "        conf commit\r\n"
                "            Commit staged config changes to nonvolatile memory.\r\n\n",
                .pxCommandInterpreter = xCommand_Configure,
                .cExpectedNumberOfParameters = -1
};
