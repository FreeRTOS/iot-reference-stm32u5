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

#include "lfs.h"

#include <string.h>


static void vSubCommand_GetConfig( char * pcWriteBuffer, size_t xWriteBufferLen, char * pcKey )
{
    lfs_t * pLFS = pxGetDefaultFsCtx();
    struct lfs_info xFileInfo = { 0 };

    int xStatus = lfs_stat( pLFS, pcKey, &xFileInfo );
    if( xStatus == LFS_ERR_NOENT || xStatus == LFS_ERR_NOTDIR )
    {
        /* Key has not been set yet */
        snprintf( pcWriteBuffer, xWriteBufferLen, "%s=\r\n", pcKey );
    }
    else
    {
        lfs_file_t xFile = { 0 };
        lfs_file_open( pLFS, &xFile, pcKey, LFS_O_RDONLY );
        int lValueSize = lfs_file_size( pLFS, &xFile );
        if( lValueSize < 0 )
        {
            snprintf( pcWriteBuffer, xWriteBufferLen, "File stat error\r\n" );
        }
        else
        {
            /* Copy value from file */
            char pcValue[ lValueSize ];
            int lNBytesRead = lfs_file_read( pLFS, &xFile, pcValue, lValueSize );
            if( lNBytesRead == lValueSize )
            {
                /* Key was set, and entirely read */
                snprintf( pcWriteBuffer, xWriteBufferLen, "%s=%s\r\n", pcKey, pcValue );
            }
            else
            {
                snprintf( pcWriteBuffer, xWriteBufferLen, "File read error\r\n");
            }
        }

        lfs_file_close( pLFS, &xFile );
    }
}
static void vSubCommand_SetConfig( char * pcWriteBuffer, size_t xWriteBufferLen, char * pcKey, char * pcValue )
{
	lfs_t * pLFS = pxGetDefaultFsCtx();
    lfs_file_t xFile = { 0 };

    int xOpenStatus = lfs_file_open( pLFS, &xFile, pcKey, LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC );
    if( xOpenStatus == LFS_ERR_OK )
    {
        size_t xValueLength = strlen( pcValue ) + 1; // Include terminator
        int lNBytesWritten = lfs_file_write( pLFS, &xFile, pcValue, xValueLength );
        if( lNBytesWritten == xValueLength )
        {
            snprintf( pcWriteBuffer, xWriteBufferLen, "%s=%s\r\n", pcKey, pcValue );
        }
        else
        {
            snprintf( pcWriteBuffer, xWriteBufferLen, "Failed to write full value\r\n");
        }

        lfs_file_close( pLFS, &xFile );
    }
    else
    {
        snprintf( pcWriteBuffer, xWriteBufferLen, "Failed to create/open file for write\r\n" );
    }
}

/* Assumes FS was already mounted */
static BaseType_t xCommand_Configure( char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString )
{
    /* Fetch mode */
    BaseType_t xModeRefLength = 0;
    char * pcModeRef = FreeRTOS_CLIGetParameter( pcCommandString, 1, &xModeRefLength );
    char pcMode[xModeRefLength + 1];
    memcpy( pcMode, pcModeRef, xModeRefLength);
    pcMode[xModeRefLength] = '\0';

    /* Fetch key */
    BaseType_t xKeyRefLength = 0;
    char * pcKeyRef = FreeRTOS_CLIGetParameter( pcCommandString, 2, &xKeyRefLength );
    char pcKey[xKeyRefLength + 1];
    memcpy( pcKey, pcKeyRef, xKeyRefLength );
    pcKey[xKeyRefLength] = '\0';

    if( 0 == strcmp( "get", pcMode ) && xKeyRefLength > 1 )
    {
        vSubCommand_GetConfig( pcWriteBuffer, xWriteBufferLen, pcKey );
    }
    else if( 0 == strcmp( "set", pcMode ) && xKeyRefLength > 1 )
    {
        /* Fetch value */
        BaseType_t xValueRefLength = 0;
        char * pcValueRef = FreeRTOS_CLIGetParameter( pcCommandString, 3, &xValueRefLength );
        char pcValue[xValueRefLength + 1];
        memcpy( pcValue, pcValueRef, xValueRefLength );
        pcValue[xValueRefLength] = '\0';

        vSubCommand_SetConfig( pcWriteBuffer, xWriteBufferLen, pcKey, pcValue );
    }
    else
    {
        strncpy( pcWriteBuffer, "Usage:\r\n conf get <key>\r\n conf set <key> <value>\r\n", xWriteBufferLen );
    }

    return pdFALSE;
}

CLI_Command_Definition_t xCommandDef_Configure =
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
