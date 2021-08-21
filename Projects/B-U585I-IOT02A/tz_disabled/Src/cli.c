#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"

#include "cli.h"
#include "logging.h"

#include <string.h>

#define CLI_COMMAND_BUFFER_SIZE 128
#define CLI_COMMAND_OUTPUT_BUFFER_SIZE configCOMMAND_INT_MAX_OUTPUT_SIZE

static IotUARTHandle_t xUART_USB = NULL;
static uint8_t ucBuffer_UART[ CLI_COMMAND_BUFFER_SIZE ] = { 0 };
static size_t xBuffer_Index = 0;
static char cOutputBuffer[ CLI_COMMAND_OUTPUT_BUFFER_SIZE ] = { 0 };

BaseType_t xCommand_Configure( char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString )
{
	return pdFALSE;
}

CLI_Command_Definition_t xCommandDef_Configure =
{
		.pcCommand = "conf",
		.pcHelpString = "conf:\n Set/Get NVM stored configuration values\n",
		.pxCommandInterpreter = xCommand_Configure,
		.cExpectedNumberOfParameters = -1
};


void Task_CLI( void * pvParameters )
{
	/* UART Bringup */
	xUART_USB = xLoggingGetIOHandle();
	if( xUART_USB == NULL )
	{
		LogError(( "NULL USB-UART Descriptor. Exiting.\n" ));
		vTaskDelete( NULL );
	}

	/* FreeRTOS CLI bringup */
	FreeRTOS_CLIRegisterCommand( &xCommandDef_Configure );

	LogInfo(( "Starting CLI...\n" ));
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
