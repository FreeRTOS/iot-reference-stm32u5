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
#include "task.h"

#include "cli.h"
#include "cli_prv.h"
#include "logging.h"
#include "stream_buffer.h"
#include "message_buffer.h"

#include <string.h>

#define HW_FIFO_LEN    8

extern volatile StreamBufferHandle_t xLogMBuf;

static char ucLogLineTxBuff[ dlMAX_PRINT_STRING_LENGTH ];
static SemaphoreHandle_t xUartTxSem = NULL;

static volatile BaseType_t xPartialCommand = pdFALSE;
/*static volatile BaseType_t xCliStreamInterrupted = pdFALSE; */

#define BUFFER_READ_TIMEOUT_MS    pdMS_TO_TICKS( 5 )

StreamBufferHandle_t xUartRxStream = NULL;
StreamBufferHandle_t xUartTxStream = NULL;

static char pcInputBuffer[ CLI_INPUT_LINE_LEN_MAX ] = { 0 };
static volatile uint32_t ulInBufferIdx = 0;

static UART_HandleTypeDef xConsoleHandle =
{
    .Instance                    = USART1,
    .Init.BaudRate               = CLI_UART_BAUD_RATE,
    .Init.WordLength             = UART_WORDLENGTH_8B,
    .Init.StopBits               = UART_STOPBITS_1,
    .Init.Parity                 = UART_PARITY_NONE,
    .Init.Mode                   = UART_MODE_TX_RX,
    .Init.HwFlowCtl              = UART_HWCONTROL_NONE,
    .Init.OverSampling           = UART_OVERSAMPLING_16,
    .Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE,
    .Init.ClockPrescaler         = UART_PRESCALER_DIV1,
    .AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT,
};

static BaseType_t xExitFlag = pdFALSE;

static TaskHandle_t xRxThreadHandle = NULL;
static TaskHandle_t xTxThreadHandle = NULL;

static void vUart1MspInitCallback( UART_HandleTypeDef * huart )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;
    RCC_PeriphCLKInitTypeDef xClockInit = { 0 };
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    if( huart == &xConsoleHandle )
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

void USART1_IRQHandler( void )
{
    HAL_UART_IRQHandler( &xConsoleHandle );
}

static void vUart1MspDeInitCallback( UART_HandleTypeDef * huart )
{
    if( huart == &xConsoleHandle )
    {
        HAL_NVIC_DisableIRQ( USART1_IRQn );
        /* De-initialize GPIOs */
        HAL_GPIO_DeInit( GPIOA, GPIO_PIN_10 | GPIO_PIN_9 );
        __HAL_RCC_USART1_CLK_DISABLE();
    }
}

static void txCompleteCallback( UART_HandleTypeDef * pxUartHandle );
static void vTxThread( void * pvParameters );
static void vRxThread( void * pvParameters );
static void rxEventCallback( UART_HandleTypeDef * pxUartHandle,
                             uint16_t usBytesRead );
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

    xUartTxSem = xSemaphoreCreateBinary();

    ( void ) HAL_UART_DeInit( &xConsoleHandle );

    xUartRxStream = xStreamBufferCreate( CLI_UART_RX_STREAM_LEN, 1 );
    xUartTxStream = xStreamBufferCreate( CLI_UART_TX_STREAM_LEN, HW_FIFO_LEN ); /*TODO maybe increase this */

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
        xHalRslt |= HAL_UARTEx_SetTxFifoThreshold( &xConsoleHandle, UART_TXFIFO_THRESHOLD_8_8 );
    }

    if( xHalRslt == HAL_OK )
    {
        xHalRslt |= HAL_UARTEx_SetRxFifoThreshold( &xConsoleHandle, UART_RXFIFO_THRESHOLD_8_8 );
    }

    /* Enable FIFO mode */
    if( xHalRslt == HAL_OK )
    {
        xHalRslt |= HAL_UARTEx_EnableFifoMode( &xConsoleHandle );
    }

    /* Start TX and RX tasks */
    xTaskCreate( vRxThread, "uartRx", 1024, NULL, 30, &xRxThreadHandle );
    xTaskCreate( vTxThread, "uartTx", 1024, NULL, 24, &xTxThreadHandle );

    ( void ) xSemaphoreGive( xUartTxSem );

    return( xHalRslt == HAL_OK );
}


/* Receive buffer A/B */
static uint8_t puxRxBufferA[ CLI_UART_RX_READ_SZ_10MS ] = { 0 };
static uint8_t puxRxBufferB[ CLI_UART_RX_READ_SZ_10MS ] = { 0 };

static uint8_t * pucNextBuffer = NULL;

#define ERROR_FLAG       ( 1U << 31 )
#define BUFFER_A_FLAG    ( 1U << 30 )
#define BUFFER_B_FLAG    ( 1U << 29 )
#define FLAGS_MASK       ( ERROR_FLAG | BUFFER_A_FLAG | BUFFER_B_FLAG )
#define READ_LEN_MASK    ( ~FLAGS_MASK )

/* */
static void rxErrorCallback( UART_HandleTypeDef * pxUartHandle )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    ( void ) xTaskNotifyIndexedFromISR( xRxThreadHandle,
                                        1,
                                        ERROR_FLAG,
                                        eSetValueWithOverwrite,
                                        &xHigherPriorityTaskWoken );

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}


static void rxEventCallback( UART_HandleTypeDef * pxUartHandle,
                             uint16_t usBytesRead )
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

        ( void ) xTaskNotifyIndexedFromISR( xRxThreadHandle,
                                            1,
                                            ulNotifyValue,
                                            eSetValueWithOverwrite,
                                            &xHigherPriorityTaskWoken );
    }
    else /* Otherwise IDLE timeout event, continue using the same buffer */
    {
        configASSERT( pucNextBuffer != NULL );
    }

    xHalStatus = HAL_UARTEx_ReceiveToIdle_IT( pxUartHandle,
                                              pucNextBuffer,
                                              CLI_UART_RX_READ_SZ_10MS );

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
        if( xTaskNotifyWaitIndexed( 1, 0, 0xFFFFFFFF, &ulNotifyValue, pdMS_TO_TICKS( 30 ) ) == pdTRUE )
        {
            size_t xBytes = ( ulNotifyValue & READ_LEN_MASK );
            size_t xBytesPushed = 0;

            if( ( xBytes > 0 ) &&
                ( ( ulNotifyValue & BUFFER_A_FLAG ) > 0 ) )
            {
                xBytesPushed = xStreamBufferSend( xUartRxStream, puxRxBufferA, xBytes, 0 );
            }
            else if( ( xBytes > 0 ) &&
                     ( ( ulNotifyValue & BUFFER_B_FLAG ) > 0 ) )
            {
                xBytesPushed = xStreamBufferSend( xUartRxStream, puxRxBufferB, xBytes, 0 );
            }
            else /* Zero bytes or error case */
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

    ( void ) vTaskNotifyGiveIndexedFromISR( xTxThreadHandle, 1, &xHigherPriorityTaskWoken );

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}


/* Uart transmit thread */
static void vTxThread( void * pvParameters )
{
    uint8_t pucTxBuffer[ CLI_UART_TX_WRITE_SZ_5MS ] = { 0 };
    HAL_StatusTypeDef xHalStatus = HAL_OK;

    size_t xBytes = 0;

    while( !xExitFlag )
    {
        /* Read up to 64 bytes
         * wait up to BUFFER_READ_TIMEOUT before getting less than 64 */
        xBytes = xStreamBufferReceive( xUartTxStream,
                                       pucTxBuffer,
                                       CLI_UART_TX_WRITE_SZ_5MS,
                                       BUFFER_READ_TIMEOUT_MS );

        /* If tx buffer is empty */
        if( xBytes == 0 )
        {
            /* Take the uart write semaphore (non-blocking) */
            if( xSemaphoreTake( xUartTxSem, 0 ) == pdTRUE )
            {
                xBytes = xMessageBufferReceive( xLogMBuf, ucLogLineTxBuff, dlMAX_PRINT_STRING_LENGTH, 0 );

                /* All log messages should be less than the maximum length */
                configASSERT( ( xBytes + CLI_OUTPUT_EOL_LEN + CLI_INPUT_LINE_LEN_MAX ) <= CLI_UART_TX_STREAM_LEN );

                /* If we got a log message to output, add it to the stream buffer to be processed */
                if( xBytes > 0 )
                {
                    if( xPartialCommand == pdTRUE )
                    {
                        /* Overwrite existing line contents */
                        ( void ) xStreamBufferSend( xUartTxStream, "\r\033[K", 4, 0 );
                    }

                    /* enqueue the log message */
                    ( void ) xStreamBufferSend( xUartTxStream, ucLogLineTxBuff, xBytes, 0 );

                    /* Add CRLF */
                    ( void ) xStreamBufferSend( xUartTxStream, CLI_OUTPUT_EOL, CLI_OUTPUT_EOL_LEN, 0 );

                    if( xPartialCommand == pdTRUE )
                    {
                        ( void ) xStreamBufferSend( xUartTxStream, CLI_PROMPT_STR, CLI_PROMPT_LEN, 0 );

                        /* Restore current command line contents */
                        if( ulInBufferIdx > 0 )
                        {
                            ( void ) xStreamBufferSend( xUartTxStream, pcInputBuffer, ulInBufferIdx, 0 );
                        }
                    }

                    ( void ) xSemaphoreGive( xUartTxSem );
                    xBytes = xStreamBufferReceive( xUartTxStream,
                                                   pucTxBuffer,
                                                   CLI_UART_TX_WRITE_SZ_5MS,
                                                   0 );
                }
                else
                {
                    ( void ) xSemaphoreGive( xUartTxSem );
                }
            }
        }

        /* Transmit if bytes available to transmit */
        if( xBytes > 0 )
        {
            ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );
            xHalStatus = HAL_UART_Transmit_IT( &xConsoleHandle, pucTxBuffer, ( uint16_t ) xBytes );
/*			configASSERT( xHalStatus == HAL_OK ); */

            if( xHalStatus == HAL_OK )
            {
                /* Wait for completion event (should be within 1 or 2 ms) */
                ( void ) ulTaskNotifyTakeIndexed( 1, pdTRUE, portMAX_DELAY );
            }
        }
    }
}

static void uart_write( const void * const pvOutputBuffer,
                        uint32_t xOutputBufferLen )
{
    size_t xBytesSent = 0;

    const uint8_t * const pcBuffer = ( uint8_t * const ) pvOutputBuffer;

    if( ( pvOutputBuffer != NULL ) &&
        ( xOutputBufferLen > 0 ) )
    {
        while( xBytesSent < xOutputBufferLen )
        {
            xBytesSent += xStreamBufferSend( xUartTxStream,
                                             ( const void * ) &( pcBuffer[ xBytesSent ] ),
                                             xOutputBufferLen - xBytesSent,
                                             portMAX_DELAY );
        }
    }

    configASSERT( xBytesSent == xOutputBufferLen );
}

/* Get at least once byte, possibly up to pcInputBuffer if the uart stays busy */
static int32_t uart_read( char * const pcInputBuffer,
                          uint32_t xInputBufferLen )
{
    int32_t ulBytesRead = 0;

    if( ( pcInputBuffer != NULL ) &&
        ( xInputBufferLen > 0 ) )
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

    if( ( pcInputBuffer != NULL ) &&
        ( xInputBufferLen > 0 ) )
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
        size_t xLength = strlen( pcString );
        uart_write( pcString, xLength );
    }
}


static int32_t uart_readline( char ** const ppcInputBuffer )
{
    int32_t lBytesWritten = 0;

    ulInBufferIdx = 0;

    BaseType_t xFoundEOL = pdFALSE;

    /* Set output buffer pointer */
    *ppcInputBuffer = pcInputBuffer;

    /* Ensure null termination */
    pcInputBuffer[ CLI_INPUT_LINE_LEN_MAX - 1 ] = '\0';

    uart_write( CLI_PROMPT_STR, CLI_PROMPT_LEN );
/*    xCliStreamInterrupted = pdFALSE; */
    xPartialCommand = pdTRUE;

    while( ulInBufferIdx < CLI_INPUT_LINE_LEN_MAX &&
           xFoundEOL == pdFALSE )
    {
        if( uart_read_timeout( &( pcInputBuffer[ ulInBufferIdx ] ), 1, portMAX_DELAY ) )
        {
            switch( pcInputBuffer[ ulInBufferIdx ] )
            {
                case '\n':
                case '\r':
                case '\00':

                    /* If we have an actual string to report, do so */
                    if( ulInBufferIdx > 0 )
                    {
                        /* Null terminate the output string */
                        pcInputBuffer[ ulInBufferIdx ] = '\0';

                        lBytesWritten = ulInBufferIdx;
                        xFoundEOL = pdTRUE;

                        if( xSemaphoreTake( xUartTxSem, portMAX_DELAY ) == pdTRUE )
                        {
                            /* Turn every line ending into an \r\n when echoing back */
                            uart_write( CLI_OUTPUT_EOL, CLI_OUTPUT_EOL_LEN );

                            xPartialCommand = pdFALSE;

                            ( void ) xSemaphoreGive( xUartTxSem );
                        }
                    }
                    /* ignore the \r or \n character if at the beginning of a string */
                    else
                    {
                        if( xSemaphoreTake( xUartTxSem, portMAX_DELAY ) == pdTRUE )
                        {
                            pcInputBuffer[ ulInBufferIdx ] = '\0';
                            ulInBufferIdx = 0;
                            ( void ) xSemaphoreGive( xUartTxSem );
                        }
                    }

                    break;

                /* Handle backspace / delete characters */
                case '\b':
                case '\x7F': /* ASCII DEL character */

                    if( ulInBufferIdx > 0 )
                    {
                        if( xSemaphoreTake( xUartTxSem, portMAX_DELAY ) == pdTRUE )
                        {
                            /* Erase current character (del or backspace) and previous character */
                            pcInputBuffer[ ulInBufferIdx ] = '\0';

                            if( ulInBufferIdx > 0 )
                            {
                                pcInputBuffer[ ulInBufferIdx - 1 ] = '\0';
                            }

                            ulInBufferIdx--;

                            uart_print( "\b \b" );
                            ( void ) xSemaphoreGive( xUartTxSem );
                        }
                    }

                    break;

                /* ctrl + c -> interrupt / clear current cli */
                case '\x03':
                    ulInBufferIdx = 0;
                    uart_write( CLI_OUTPUT_EOL CLI_PROMPT_STR, CLI_OUTPUT_EOL_LEN + CLI_PROMPT_LEN );
                    break;

                /* Otherwise consume the character as normal */
                default:

                    if( xSemaphoreTake( xUartTxSem, portMAX_DELAY ) == pdTRUE )
                    {
                        uart_write( &( pcInputBuffer[ ulInBufferIdx ] ), 1 );
                        ulInBufferIdx++;

                        ( void ) xSemaphoreGive( xUartTxSem );
                    }

                    break;
            }
        }
    }

    return lBytesWritten;
}

static void uart_lock( void )
{
    xSemaphoreTake( xUartTxSem, portMAX_DELAY );
}

static void uart_unlock( void )
{
    xSemaphoreGive( xUartTxSem );
}

const ConsoleIO_t xConsoleIO =
{
    .read         = uart_read,
    .write        = uart_write,
    .lock         = uart_lock,
    .unlock       = uart_unlock,
    .read_timeout = uart_read_timeout,
    .print        = uart_print,
    .readline     = uart_readline
};
