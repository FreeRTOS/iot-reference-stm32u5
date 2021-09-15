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

#define WRITE_BUFFER_LEN 128

static void vSubCommand_GetConfig( ConsoleIO_t * pxConsoleIO, char * pcKey )
{
    char pcWriteBuffer[ WRITE_BUFFER_LEN ];

    KVStoreKey_t xKey = kvStringToKey( pcKey );
    KVStoreValueType_t xKvType = KVStore_getType( xKey );

    int32_t lResponseLen = 0;

    switch( xKvType )
    {
    case KV_TYPE_BASE_T:
    {
        BaseType_t xValue = KVStore_getBase( xKey, NULL );
        lResponseLen = snprintf( pcWriteBuffer, WRITE_BUFFER_LEN, "%s=%ld\r\n", pcKey, xValue );
        break;
    }
    case KV_TYPE_UBASE_T:
    {
        UBaseType_t xValue = KVStore_getUBase( xKey, NULL );
        lResponseLen = snprintf( pcWriteBuffer, WRITE_BUFFER_LEN, "%s=%lu\r\n", pcKey, xValue );
        break;
    }
    case KV_TYPE_INT32:
    {
        int32_t lValue = KVStore_getInt32( xKey, NULL );
        lResponseLen = snprintf( pcWriteBuffer, WRITE_BUFFER_LEN, "%s=%ld\r\n", pcKey, lValue );
        break;
    }
    case KV_TYPE_UINT32:
    {
        uint32_t ulValue = KVStore_getUInt32( xKey, NULL );
        lResponseLen = snprintf( pcWriteBuffer, WRITE_BUFFER_LEN, "%s=%lu\r\n", pcKey, ulValue );
        break;
    }
    case KV_TYPE_STRING:
    case KV_TYPE_BLOB:
    {
        char * pcWorkPtr = pcWriteBuffer;
        pcWorkPtr = strncpy( pcWorkPtr, pcKey, WRITE_BUFFER_LEN );

        lResponseLen = ( ( uintptr_t ) pcWorkPtr - ( uintptr_t ) pcWriteBuffer );

        if( lResponseLen < WRITE_BUFFER_LEN )
        {
            *pcWorkPtr = '=';
            pcWorkPtr++;
            lResponseLen++;
        }

        if(lResponseLen  < WRITE_BUFFER_LEN )
        {
            lResponseLen += KVStore_getString( xKey, pcWorkPtr, WRITE_BUFFER_LEN - lResponseLen - 1 );
        }
    }
    case KV_TYPE_LAST:
    case KV_TYPE_NONE:
        lResponseLen = snprintf( pcWriteBuffer, WRITE_BUFFER_LEN, "%s=\r\n", pcKey );
        break;
    }

    if( lResponseLen > 0 )
    {
        /* Correct lResponseLen for any responses that were truncated by snprintf */
        if( lResponseLen > WRITE_BUFFER_LEN )
        {
            lResponseLen = WRITE_BUFFER_LEN;
        }
        pcWriteBuffer[ WRITE_BUFFER_LEN - 1 ] = 0;


        /* Write function does not require null termination */
        pxConsoleIO->write( pcWriteBuffer, ( uint32_t ) lResponseLen );
    }
}
static void vSubCommand_SetConfig( ConsoleIO_t * pxConsoleIO, char * pcKey, char * pcValue )
{
    //	lfs_t * pLFS = pxGetDefaultFsCtx();
    //    lfs_file_t xFile = { 0 };
    //
    //    int xOpenStatus = lfs_file_open( pLFS, &xFile, pcKey, LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC );
    //    if( xOpenStatus == LFS_ERR_OK )
    //    {
    //        size_t xValueLength = strlen( pcValue ) + 1; // Include terminator
    //        int lNBytesWritten = lfs_file_write( pLFS, &xFile, pcValue, xValueLength );
    //        if( lNBytesWritten == xValueLength )
    //        {
    //            snprintf( pcWriteBuffer, xWriteBufferLen, "%s=%s\r\n", pcKey, pcValue );
    //        }
    //        else
    //        {
    //            snprintf( pcWriteBuffer, xWriteBufferLen, "Failed to write full value\r\n");
    //        }
    //
    //        lfs_file_close( pLFS, &xFile );
    //    }
    //    else
    //    {
    //        snprintf( pcWriteBuffer, xWriteBufferLen, "Failed to create/open file for write\r\n" );
    //    }
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
    memcpy( pcKey, pcKeyRef, xKeyRefLength );
    pcKey[xKeyRefLength] = '\0';

    if( 0 == strcmp( "get", pcMode ) && xKeyRefLength > 1 )
    {
        vSubCommand_GetConfig( pxConsoleIO, pcKey );
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
                "    Set/Get NVM stored configuration values\r\n"
                "    Usage:\r\n"
                "        conf get <key>\r\n"
                "        conf set <key> <value>\r\n",
                .pxCommandInterpreter = xCommand_Configure,
                .cExpectedNumberOfParameters = -1
};
