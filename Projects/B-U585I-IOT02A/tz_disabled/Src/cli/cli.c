#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"

#include "cli.h"
#include "logging.h"

#include "lfs.h"
#include "lfs_port.h"

#include <string.h>

#define CLI_COMMAND_BUFFER_SIZE 128
#define CLI_COMMAND_OUTPUT_BUFFER_SIZE configCOMMAND_INT_MAX_OUTPUT_SIZE

static IotUARTHandle_t xUART_USB = NULL;
static uint8_t ucBuffer_UART[ CLI_COMMAND_BUFFER_SIZE ] = { 0 };
static size_t xBuffer_Index = 0;
char cOutputBuffer[ CLI_COMMAND_OUTPUT_BUFFER_SIZE ] = { 0 };

static void vSubCommand_GetConfig( char * pcWriteBuffer, size_t xWriteBufferLen, char * pcKey )
{
    lfs_t * pLFS = lfs_port_get_fs_handle();
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
    lfs_t * pLFS = lfs_port_get_fs_handle();
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
BaseType_t xCommand_Configure( char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString )
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


void Task_CLI( void * pvParameters )
{
    /* UART Bringup */
    xUART_USB = xLoggingGetIOHandle();
    if( xUART_USB == NULL )
    {
        LogError(( "NULL USB-UART Descriptor. Exiting.\r\n" ));
        vTaskDelete( NULL );
    }

    /* FreeRTOS CLI bringup */
    FreeRTOS_CLIRegisterCommand( &xCommandDef_Configure );

    LogInfo(( "Starting CLI...\r\n" ));
    uint8_t ucByteIn = 0;
    int32_t lReadStatus = 0;
    int32_t lioctlStatus = 0;
    int32_t lNBytesRead = 0;
    size_t xNBytesOut = 0;
    while( 1 )
    {
        lReadStatus = iot_uart_read_sync( xUART_USB, &ucByteIn, 1 );
        lioctlStatus = iot_uart_ioctl( xUART_USB, eGetRxNoOfbytes, &lNBytesRead );
        if( lReadStatus == IOT_UART_SUCCESS && lioctlStatus == IOT_UART_SUCCESS && lNBytesRead > 0 )
        {
            iot_uart_write_sync( xUART_USB, &ucByteIn, 1);
            switch( ucByteIn )
            {
                case '\r':
                case '\n':
                    ucBuffer_UART[xBuffer_Index] = '\0';
                    while( pdTRUE == FreeRTOS_CLIProcessCommand( ucBuffer_UART, cOutputBuffer, CLI_COMMAND_OUTPUT_BUFFER_SIZE ) )
                    {
                        xNBytesOut = strnlen( (char*)cOutputBuffer, CLI_COMMAND_OUTPUT_BUFFER_SIZE );
                        iot_uart_write_sync( xUART_USB, cOutputBuffer, xNBytesOut );
                    }
                    /* Flush remaining */
                    xNBytesOut = strnlen( (char*)cOutputBuffer, CLI_COMMAND_OUTPUT_BUFFER_SIZE );
                    iot_uart_write_sync( xUART_USB, cOutputBuffer, xNBytesOut );
                    cOutputBuffer[0] = '\0';

                    xBuffer_Index = 0;
                    break;

                default:
                    if( xBuffer_Index < CLI_COMMAND_BUFFER_SIZE - 1 )
                    {
                        ucBuffer_UART[xBuffer_Index++] = ucByteIn;
                    }
                    break;
            }
        }

        vTaskDelay( pdMS_TO_TICKS( 1 ) );
    }
}
