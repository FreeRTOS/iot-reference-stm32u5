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
#include "cli_prv.h"
#include "stream_buffer.h"

#include <string.h>

#include "FreeRTOS_CLI_Console.h"


#define HW_FIFO_LEN 		8

extern volatile StreamBufferHandle_t xLogBuf;

/*
 * 115200 bits      1 byte             1 second
 * ------------ X --------------- X  -------- = 11.52 bytes / ms
 *    second	   8 + 1 + 1 bits     1000 ms
 *
 * Up to ~115 bytes per 10ms
 */
#define CONSOLE_BAUD_RATE 		( 115200 )

/* 8 bits per frame + 1 start + 1 stop bit */
#define CONSOLE_BITS_PER_FRAME 	( 8 + 1 + 1 )

#define CONSOLE_FRAMES_PER_SEC	( CONSOLE_BAUD_RATE / CONSOLE_BITS_PER_FRAME )

#define RX_HW_TIMEOUT_MS 	10

/* 115.2 bytes per 10 ms */
#define BYTES_PER_RX_TIME	( CONSOLE_FRAMES_PER_SEC * RX_HW_TIMEOUT_MS / 1000 )

#define RX_READ_SZ_10MS		128

#define TX_WRITE_SZ_5MS		64

#define RX_STREAM_LEN 512
#define TX_STREAM_LEN 512

#define BUFFER_READ_TIMEOUT_MS pdMS_TO_TICKS( 5 )


StreamBufferHandle_t xUartRxStream = NULL;
StreamBufferHandle_t xUartTxStream = NULL;

UART_HandleTypeDef xConsoleHandle =
{
	.Instance = USART1,
	.Init.BaudRate = CONSOLE_BAUD_RATE,
	.Init.WordLength = UART_WORDLENGTH_8B,
	.Init.StopBits = UART_STOPBITS_1,
	.Init.Parity = UART_PARITY_NONE,
	.Init.Mode = UART_MODE_TX_RX,
	.Init.HwFlowCtl = UART_HWCONTROL_NONE,
	.Init.OverSampling = UART_OVERSAMPLING_16,
	.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE,
	.Init.ClockPrescaler = UART_PRESCALER_DIV1,
	.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT,
};

static volatile BaseType_t xExitFlag = pdFALSE;

volatile BaseType_t xPartialCommand = pdFALSE;

static TaskHandle_t xRxThreadHandle = NULL;
static TaskHandle_t xTxThreadHandle = NULL;

static void vUart1MspInitCallback( UART_HandleTypeDef *huart )
{
	HAL_StatusTypeDef xHalStatus = HAL_OK;
	RCC_PeriphCLKInitTypeDef xClockInit = { 0 };
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	if( huart == &xConsoleHandle  )
	{
		__HAL_RCC_USART1_CLK_DISABLE();

		xClockInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
		xClockInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;

		xHalStatus = HAL_RCCEx_PeriphCLKConfig( &xClockInit );

		if( xHalStatus == HAL_OK )
		{
			__HAL_RCC_USART1_CLK_ENABLE();
		}

		/*
		 * Enable GPIOA clock
		 * Mux RX/TX GPIOs to USART1 Alternate Function: GPIO_AF7_USART1
		 * PA10 -> USART_RX
		 * PA9 -> USART1_TX
		 */

		__HAL_RCC_GPIOA_CLK_ENABLE();

		GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
		GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
		GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
		HAL_GPIO_Init( GPIOA, &GPIO_InitStruct );

		HAL_NVIC_SetPriority( USART1_IRQn, 5, 1 );
		HAL_NVIC_EnableIRQ( USART1_IRQn );
	}
}

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler( &xConsoleHandle );
}

static void vUart1MspDeInitCallback( UART_HandleTypeDef *huart )
{
	if( huart == &xConsoleHandle )
	{
		HAL_NVIC_DisableIRQ( USART1_IRQn );
		/* Deinit GPIOs */
		HAL_GPIO_DeInit( GPIOA, GPIO_PIN_10 | GPIO_PIN_9 );
		__HAL_RCC_USART1_CLK_DISABLE();
	}
}

static void txCompleteCallback( UART_HandleTypeDef * pxUartHandle );
static void vTxThread( void * pvParameters );
static void vRxThread( void * pvParameters );
static void rxEventCallback( UART_HandleTypeDef * pxUartHandle, uint16_t usBytesRead );
static void rxErrorCallback( UART_HandleTypeDef * pxUartHandle );


/* Should only be called before the scheduler has been initialized / after an assertion has occurred */
UART_HandleTypeDef * vInitUartEarly( void )
{
	( void ) HAL_UART_DeInit( &xConsoleHandle );

	( void ) HAL_UART_RegisterCallback( &xConsoleHandle, HAL_UART_MSPINIT_CB_ID, vUart1MspInitCallback );
	( void ) HAL_UART_RegisterCallback( &xConsoleHandle, HAL_UART_MSPDEINIT_CB_ID, vUart1MspDeInitCallback );
	( void ) HAL_UART_Init( &xConsoleHandle );
	return &xConsoleHandle;
}

BaseType_t xInitConsoleUart( void )
{
	HAL_StatusTypeDef xHalRslt = HAL_OK;

	( void ) HAL_UART_DeInit( &xConsoleHandle );

	xUartRxStream = xStreamBufferCreate( RX_STREAM_LEN, 1 );
	xUartTxStream = xStreamBufferCreate( TX_STREAM_LEN, HW_FIFO_LEN ); //TODO maybe increase this

	xHalRslt |= HAL_UART_RegisterCallback( &xConsoleHandle, HAL_UART_MSPINIT_CB_ID, vUart1MspInitCallback );
	xHalRslt |= HAL_UART_RegisterCallback( &xConsoleHandle, HAL_UART_MSPDEINIT_CB_ID, vUart1MspDeInitCallback );

	if( xHalRslt == HAL_OK )
	{
		/* HAL_UART_Init calls mspInitCallback internally */
		xHalRslt = HAL_UART_Init( &xConsoleHandle );
	}

	/* Register callbacks */
	if( xHalRslt == HAL_OK )
	{
		xHalRslt |= HAL_UART_RegisterCallback( &xConsoleHandle, HAL_UART_TX_COMPLETE_CB_ID, txCompleteCallback );

		xHalRslt |= HAL_UART_RegisterCallback( &xConsoleHandle, HAL_UART_ERROR_CB_ID, rxErrorCallback );
		xHalRslt |= HAL_UART_RegisterRxEventCallback( &xConsoleHandle, rxEventCallback );
	}

	/* Set the FIFO thresholds */
	if( xHalRslt == HAL_OK )
	{
		xHalRslt |= HAL_UARTEx_SetTxFifoThreshold( &xConsoleHandle, UART_TXFIFO_THRESHOLD_8_8);
	}

	if( xHalRslt == HAL_OK )
	{
		xHalRslt |= HAL_UARTEx_SetRxFifoThreshold( &xConsoleHandle, UART_RXFIFO_THRESHOLD_8_8 );
	}

	/* Enable fifo mode */
	if( xHalRslt == HAL_OK )
	{
		xHalRslt |= HAL_UARTEx_EnableFifoMode( &xConsoleHandle );
	}

	/* Start TX and RX tasks */
	xTaskCreate( vRxThread, "uartRx", 1024, NULL, 30, &xRxThreadHandle );
	xTaskCreate( vTxThread, "uartTx", 1024, NULL, 24, &xTxThreadHandle );

	return( xHalRslt == HAL_OK );
}


/* Receive buffer A/B */
static uint8_t puxRxBufferA[ RX_READ_SZ_10MS ] = { 0 };
static uint8_t puxRxBufferB[ RX_READ_SZ_10MS ] = { 0 };

static volatile uint8_t * pucNextBuffer = NULL;

#define ERROR_FLAG	 	( 0b1 << 31 )
#define BUFFER_A_FLAG 	( 0b1 << 30 )
#define BUFFER_B_FLAG 	( 0b1 << 29 )
#define FLAGS_MASK 		( ERROR_FLAG | BUFFER_A_FLAG | BUFFER_B_FLAG )
#define READ_LEN_MASK 	( ~FLAGS_MASK )

/* */
static void rxErrorCallback( UART_HandleTypeDef * pxUartHandle )
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	( void ) xTaskNotifyFromISR( xRxThreadHandle,
								 ERROR_FLAG,
								 eSetValueWithOverwrite,
								 &xHigherPriorityTaskWoken );

	portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}


static void rxEventCallback( UART_HandleTypeDef * pxUartHandle, uint16_t usBytesRead )
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	HAL_StatusTypeDef xHalStatus = HAL_OK;

	/* Check if we read some data or timed out */
	if( usBytesRead > 0 )
	{
		uint32_t ulNotifyValue = usBytesRead;

		/* Determine next buffer to write to */
		configASSERT( pucNextBuffer != NULL );

		if( pucNextBuffer == puxRxBufferA )
		{
			ulNotifyValue |= BUFFER_A_FLAG;
			pucNextBuffer = puxRxBufferB;
		}
		else if( pucNextBuffer == puxRxBufferB )
		{
			ulNotifyValue |= BUFFER_B_FLAG;
			pucNextBuffer = puxRxBufferA;
		}
		else
		{
			/* pxUartHandle->pRxBuffPtr contents unrecognized */
			configASSERT( 0 );
		}

		( void ) xTaskNotifyFromISR( xRxThreadHandle,
									 ulNotifyValue,
									 eSetValueWithOverwrite,
									 &xHigherPriorityTaskWoken );

	}
	else	/* Otherwise IDLE timeout event, continue using the same buffer */
	{
		configASSERT( pucNextBuffer != NULL );
	}

	xHalStatus = HAL_UARTEx_ReceiveToIdle_IT( pxUartHandle,
											  pucNextBuffer,
											  RX_READ_SZ_10MS );

	configASSERT( xHalStatus == HAL_OK );

	portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

static void vRxThread( void * pvParameters )
{
	/* Start the initial receive into buffer A */
	pucNextBuffer = puxRxBufferA;
	rxEventCallback( &xConsoleHandle, 0 );

	while( !xExitFlag )
	{
		uint32_t ulNotifyValue = 0;
		/* Wait for completion event */
		if( xTaskNotifyWait( 0, 0xFFFFFFFF, &ulNotifyValue, pdMS_TO_TICKS( 30 ) ) == pdTRUE )
		{
			size_t xBytes = ( ulNotifyValue & READ_LEN_MASK );
			size_t xBytesPushed = 0;

			if( xBytes > 0 &&
				( ulNotifyValue & BUFFER_A_FLAG ) > 0 )
			{
				xBytesPushed = xStreamBufferSend( xUartRxStream, puxRxBufferA, xBytes, 0 );
			}
			else if( xBytes > 0 &&
					 ( ulNotifyValue & BUFFER_B_FLAG ) > 0 )
			{
				xBytesPushed = xStreamBufferSend( xUartRxStream, puxRxBufferB, xBytes, 0 );
			}
			else	/* Zero bytes or error case */
			{
				xBytesPushed = 0;
			}

			/* Log warning if failed to add data */
			if( xBytesPushed != xBytes )
			{
				LogWarn( "Dropped %ld bytes. Console receive buffer full.", xBytes - xBytesPushed );
			}
		}
	}
}

/* */
static void txCompleteCallback( UART_HandleTypeDef * pxUartHandle )
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	( void ) vTaskNotifyGiveFromISR( xTxThreadHandle, &xHigherPriorityTaskWoken );

	portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}


/* Uart transmit thread */
static void vTxThread( void * pvParameters )
{
	uint8_t pucTxBuffer[ TX_WRITE_SZ_5MS ] = { 0 };
	HAL_StatusTypeDef xHalStatus = HAL_OK;

	size_t xBytes = 0;
	while( !xExitFlag )
	{
		/* Read up to 64 bytes
		 * wait up to BUFFER_READ_TIMEOUT before getting less than 64 */
		xBytes = xStreamBufferReceive( xUartTxStream,
									   pucTxBuffer,
									   TX_WRITE_SZ_5MS,
									   BUFFER_READ_TIMEOUT_MS );

		if( xBytes == 0 &&
			xPartialCommand == pdFALSE )
		{
			xBytes = xStreamBufferReceive( xLogBuf, pucTxBuffer, TX_WRITE_SZ_5MS, 0 );
		}

		/* Transmit if bytes received */
		if( xBytes > 0 )
		{
			xHalStatus = HAL_UART_Transmit_IT( &xConsoleHandle, pucTxBuffer, ( uint16_t ) xBytes );
			configASSERT( xHalStatus == HAL_OK );

			/* Wait for completion event (should be within 1 or 2 ms) */
			( void ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
		}
	}
}

static void uart_write( const void * const pvOutputBuffer,
                 	    uint32_t xOutputBufferLen )
{
	if( pvOutputBuffer != NULL &&
		xOutputBufferLen > 0 )
	{
		size_t xBytesSent = xStreamBufferSend( xUartTxStream,
		                                       ( const void * ) pvOutputBuffer,
											   xOutputBufferLen,
											   portMAX_DELAY );
		configASSERT( xBytesSent == xOutputBufferLen );
	}
}

/* Get at least once byte, possibly up to pcInputBuffer if the uart stays busy */
static int32_t uart_read( char * const pcInputBuffer,
                   	   	  uint32_t xInputBufferLen )
{
	int32_t ulBytesRead = 0;
	if( pcInputBuffer != NULL &&
		xInputBufferLen > 0 )
	{
		ulBytesRead = xStreamBufferReceive( xUartRxStream,
											pcInputBuffer,
											xInputBufferLen,
											portMAX_DELAY );
	}
	return ulBytesRead;
}

/* Get at least once byte, possibly up to pcInputBuffer if the uart stays busy */
static int32_t uart_read_timeout( char * const pcInputBuffer,
                   	   	  	  	  uint32_t xInputBufferLen,
								  TickType_t xTimeout )
{
	int32_t ulBytesRead = 0;
	if( pcInputBuffer != NULL &&
		xInputBufferLen > 0 )
	{
		ulBytesRead = xStreamBufferReceive( xUartRxStream,
											pcInputBuffer,
											xInputBufferLen,
											xTimeout );
	}
	return ulBytesRead;
}

static void uart_print( const char * const pcString )
{
	if( pcString != NULL )
	{
		( void ) xStreamBufferSend( xUartTxStream,
								    ( const void * ) pcString,
								    strnlen( pcString, TX_STREAM_LEN ),
									portMAX_DELAY );
	}
}

const ConsoleIO_t xConsoleIODesc =
{
	.read = uart_read,
	.write = uart_write,
	.read_timeout = uart_read_timeout,
	.print = uart_print,
};
