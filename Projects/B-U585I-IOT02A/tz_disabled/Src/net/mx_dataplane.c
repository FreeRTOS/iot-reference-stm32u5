/*
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 */

#include "logging_levels.h"
#define LOG_LEVEL LOG_ERROR

#include "logging.h"

#include "main.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "event_groups.h"
#include "stdbool.h"
#include "stm32u5xx_hal.h"
#include "message_buffer.h"
#include "atomic.h"

#include "mx_ipc.h"
#include "mx_prv.h"


#define EVT_SPI_DONE            0x8
#define EVT_SPI_ERROR           0x10

#define EVT_SPI_FLOW            0x2

#define SPI_EVT_DMA_IDX         1
#define SPI_EVT_FLOW_IDX        2

static MxDataplaneCtx_t * volatile pxSpiCtx = NULL;

uint32_t prvGetNextRequestID( void )
{
    uint32_t ulRequestId = 0;

    if( pxSpiCtx != NULL )
    {
        ulRequestId= Atomic_Increment_u32( &( pxSpiCtx->ulLastRequestId ) );

        /* Avoid ulRequestId == 0 */
        if( ulRequestId == 0 )
        {
            ulRequestId = Atomic_Increment_u32( &( pxSpiCtx->ulLastRequestId ) );
        }
    }

    return ulRequestId;
}


/* Callback functions */
static void spi_transfer_done_callback( SPI_HandleTypeDef *hspi )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t rslt = pdFALSE;

    if( pxSpiCtx != NULL )
    {
        rslt = xTaskNotifyIndexedFromISR( pxSpiCtx->xDataPlaneTaskHandle,
                                          SPI_EVT_DMA_IDX,
                                          EVT_SPI_DONE,
                                          eSetBits,
                                          &xHigherPriorityTaskWoken );
        configASSERT( rslt == pdTRUE );

        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

static void spi_transfer_error_callback( SPI_HandleTypeDef *hspi )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    BaseType_t rslt = pdFALSE;

    if( pxSpiCtx != NULL )
    {
        rslt = xTaskNotifyIndexedFromISR( pxSpiCtx->xDataPlaneTaskHandle,
                                          SPI_EVT_DMA_IDX,
                                          EVT_SPI_ERROR,
                                          eSetBits,
                                          &xHigherPriorityTaskWoken );
        configASSERT( rslt == pdTRUE );

        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

/* Notify / IRQ pin transition means data is ready */
static void spi_notify_callback( void * pvContext )
{
    MxDataplaneCtx_t * pxCtx = ( MxDataplaneCtx_t * ) pvContext;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if( pxSpiCtx != NULL )
    {
        vTaskNotifyGiveIndexedFromISR( pxCtx->xDataPlaneTaskHandle,
                                       DATA_WAITING_IDX,
                                       &xHigherPriorityTaskWoken );

        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

static void spi_flow_callback( void * pvContext )
{
    MxDataplaneCtx_t * pxCtx = ( MxDataplaneCtx_t * ) pvContext;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if( pxSpiCtx != NULL )
    {
        vTaskNotifyGiveIndexedFromISR( pxCtx->xDataPlaneTaskHandle,
                                       SPI_EVT_FLOW_IDX,
                                       &xHigherPriorityTaskWoken );

        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

/* Handle GPIO operations */
static inline void vGpioClear( const IotMappedPin_t * gpio )
{
    HAL_GPIO_WritePin( gpio->xPort, gpio->xPinMask, GPIO_PIN_RESET );
}

static inline void vGpioSet( const IotMappedPin_t * gpio )
{
    HAL_GPIO_WritePin( gpio->xPort, gpio->xPinMask, GPIO_PIN_SET );
}

static inline BaseType_t xGpioGet( const IotMappedPin_t * gpio )
{
    return (BaseType_t) HAL_GPIO_ReadPin( gpio->xPort, gpio->xPinMask );
}

static inline void vDoHardReset( MxDataplaneCtx_t * pxCtx )
{
    vGpioClear( pxCtx->gpio_reset );
    vTaskDelay(100);

    vGpioSet( pxCtx->gpio_reset );
    vTaskDelay( 5200 );
}

/* SPI protocol definitions */
#define MX_SPI_WRITE        ( 0x0A )
#define MX_SPI_READ         ( 0x0B )

static inline BaseType_t xWaitForSPIEvent( TickType_t xTimeout )
{
    BaseType_t xWaitResult = pdFALSE;
    BaseType_t xReturnValue = pdFALSE;
    uint32_t ulNotifiedValue = 0;

    LogDebug( "Starting wait for SPI DMA event, Timeout=%d", xTimeout );

    xWaitResult = xTaskNotifyWaitIndexed( SPI_EVT_DMA_IDX, 0, 0xFFFFFFFF, &ulNotifiedValue, xTimeout );

    if( xWaitResult == pdTRUE )
    {
        if( ulNotifiedValue & EVT_SPI_DONE )
        {
            LogDebug("SPI done event received.");
            xReturnValue = pdTRUE;
        }

        if( ulNotifiedValue & EVT_SPI_ERROR )
        {
            LogError( "SPI error event received." );
            xReturnValue = pdFALSE;
        }

        if( ( ulNotifiedValue & ( EVT_SPI_ERROR | EVT_SPI_DONE ) ) == 0 )
        {
            LogError( "Timeout while waiting for SPI event." );
            xReturnValue = pdFALSE;
        }
    }
    return xReturnValue;
}

static inline void vPrintBuffer( uint8_t * pcBuffer,
                                 uint32_t usDataLen )
{
    char ucPrintBuf[ usDataLen * 2 + 1 ];
    for( uint32_t i = 0; i < usDataLen; i++ )
    {
        snprintf( &ucPrintBuf[ 2 * i ], 3, "%02X", pcBuffer[i] );
    }
    ucPrintBuf[ usDataLen * 2 ] = 0;
    LogDebug("%s", ucPrintBuf);
}

/*
 * @brief Exchange SPIHeader_t headers with the wifi module.
 * */
static inline BaseType_t xDoSpiHeaderTransfer( MxDataplaneCtx_t * pxCtx,
                                               uint16_t * psTxLen,
                                               uint16_t * psRxLen )
{
    HAL_StatusTypeDef xHalStatus = HAL_ERROR;

    SPIHeader_t xRxHeader = { 0 };
    SPIHeader_t xTxHeader = { 0 };

    xTxHeader.type = MX_SPI_WRITE;
    xTxHeader.len = *psTxLen;
    xTxHeader.lenx = ~( xTxHeader.len );

    vPrintBuffer( (void *) &xTxHeader, sizeof( SPIHeader_t ) );

    ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );

    xHalStatus = HAL_SPI_TransmitReceive_DMA( pxCtx->pxSpiHandle,
                                              ( uint8_t * ) &xTxHeader,
                                              ( uint8_t * ) &xRxHeader,
                                              sizeof( SPIHeader_t ) );

   /* Exchange headers with the module */
   if( xHalStatus != HAL_OK )
   {
       LogError( "Error returned by HAL_SPI_TransmitReceive_DMA call during transfer. xHalStatus=%d", xHalStatus );
   }
   else
   {
       xHalStatus = ( xWaitForSPIEvent( MX_SPI_EVENT_TIMEOUT ) == pdTRUE ) ? HAL_OK : HAL_ERROR;
   }

   vPrintBuffer( (void *) &xRxHeader, sizeof( SPIHeader_t ) );

   if( xHalStatus == HAL_OK &&
       xRxHeader.len < MX_MAX_MESSAGE_LEN &&
       xRxHeader.type == MX_SPI_READ &&
       ( ( xRxHeader.len ) ^ ( xRxHeader.lenx ) ) == 0xFFFF )
    {
       *psRxLen = xRxHeader.len;
    }
    else
    {
        if( xRxHeader.type == MX_SPI_READ &&
            xRxHeader.len != 0 )
        {
            LogError( "RX header validation failed. len: %d, lenx: %d, xord: %d, type: %d, xHalStatus: %d",
                       xRxHeader.len, xRxHeader.lenx,  ( xRxHeader.len ) ^ ( xRxHeader.lenx ), xRxHeader.type, xHalStatus );
        }
        *psRxLen = 0;
    }

   return( xHalStatus == HAL_OK );
}

static inline BaseType_t xReceiveMessage( MxDataplaneCtx_t * pxCtx,
                                          uint8_t * pucRxBuffer,
                                          uint32_t ulRxDataLen )
{
    HAL_StatusTypeDef xHalStatus;
    configASSERT( pxCtx != NULL );
    configASSERT( pucRxBuffer != NULL );
    configASSERT( ulRxDataLen > 0 );

    ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );

    xHalStatus = HAL_SPI_Receive_DMA( pxCtx->pxSpiHandle,
                                      pucRxBuffer,
                                      ulRxDataLen );

#if LOG_LEVEL >= LOG_DEBUG
    char ucPrintBuf[ ulRxDataLen * 3 + 1 ];

    for( uint32_t i = 0; i < ulRxDataLen; i++ )
    {
        snprintf( &ucPrintBuf[ 3 * i ], 4, "%02X ", pucRxBuffer[ i ] );
    }

    ucPrintBuf[ ulRxDataLen * 3 ] = 0;

    LogDebug( "<-- %s", ucPrintBuf );
#endif

    vPrintBuffer( pucRxBuffer, ulRxDataLen );

    xHalStatus = ( xWaitForSPIEvent( MX_SPI_EVENT_TIMEOUT ) == pdTRUE ) ? HAL_OK : HAL_ERROR;

    return xHalStatus == HAL_OK;
}

static inline BaseType_t xTransmitMessage( MxDataplaneCtx_t * pxCtx,
                                           uint8_t * pucTxBuffer,
                                           uint32_t usTxDataLen )
{
    HAL_StatusTypeDef xHalStatus;
    configASSERT( pxCtx != NULL );
    configASSERT( pucTxBuffer != NULL );
    configASSERT( usTxDataLen > 0 );


    ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );

    xHalStatus = HAL_SPI_Transmit_DMA( pxCtx->pxSpiHandle,
                                       pucTxBuffer,
                                       usTxDataLen );

#if LOG_LEVEL >= LOG_DEBUG
    char ucPrintBuf[ usTxDataLen * 3 + 1 ];

    for( uint32_t i = 0; i < usTxDataLen; i++ )
    {
        snprintf( &ucPrintBuf[ 3 * i ], 4, "%02X ", pucTxBuffer[ i ] );
    }

    ucPrintBuf[ usTxDataLen * 3 ] = 0;

    LogDebug( "--> %s", ucPrintBuf );
#endif

    xHalStatus = ( xWaitForSPIEvent( MX_SPI_EVENT_TIMEOUT ) == pdTRUE ) ? HAL_OK : HAL_ERROR;

    return xHalStatus == HAL_OK;
}

//static inline BaseType_t xTransmitPacket( MxDataplaneCtx_t * pxCtx,
//                                          uint8_t * pxTxBuffer,
//                                          uint8_t * pxRxBuffer,
//                                          uint32_t usTxDataLen,
//                                          uint32_t usRxDataLen )
//{
//    HAL_StatusTypeDef xHalStatus = HAL_OK;
////
//    /* Split into up to two dma transactions if both rx and tx ops are necessary */
//    if( usTxDataLen > 0 &&
//        usRxDataLen > 0 )
//    {
//        LogDebug("Starting simultaneous transmit");
//        /* Perform TXRX transaction for common data length */
//        if( usTxDataLen >= usRxDataLen )
//        {
//            LogDebug( "TXRX %d Bytes", usRxDataLen );
//
//            ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );
//
//            xHalStatus = HAL_SPI_TransmitReceive_DMA( pxCtx->pxSpiHandle,
//                                                      pxTxBuffer,
//                                                      pxRxBuffer,
//                                                      usRxDataLen );
//
//        }
//        else if( usRxDataLen > usTxDataLen )
//        {
//            LogDebug( "TXRX %d Bytes", usTxDataLen );
//
//            ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );
//
//            xHalStatus = HAL_SPI_TransmitReceive_DMA( pxCtx->pxSpiHandle,
//                                                      pxTxBuffer,
//                                                      pxRxBuffer,
//                                                      usTxDataLen );
//        }
//        else
//        {
//            /* Empty */
//        }
//
//        if( xHalStatus == HAL_OK )
//        {
//            /* Wait for DMA event */
//            xHalStatus = ( xWaitForSPIEvent( MX_SPI_EVENT_TIMEOUT ) == pdTRUE ) ? HAL_OK : HAL_ERROR;
//        }
//        else
//        {
//            LogError( "Error occurred during HAL_SPI_TransmitReceive_DMA transaction: %d", xHalStatus );
//        }
//    }
//
//    if( xHalStatus == HAL_OK )
//    {
//        /* Transmit only case */
//        if( usTxDataLen > usRxDataLen )
//        {
//            LogDebug( "Starting DMA TX. Offset: %d, Length: %d Bytes",
//                      usRxDataLen,
//                      ( usTxDataLen - usRxDataLen ) );
//
//            ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );
//
//            xHalStatus = HAL_SPI_Transmit_DMA( pxCtx->pxSpiHandle,
//                                               &( pxTxBuffer[ usRxDataLen ] ),
//                                               ( usTxDataLen - usRxDataLen ) );
//            char dbgBuf[ usTxDataLen * 2 + 1 ];
//            for( uint32_t i = 0; i < usTxDataLen; i++ )
//            {
//                snprintf(&dbgBuf[2*i],3,"%02X",pxTxBuffer[i]);
//            }
//            dbgBuf[ usTxDataLen * 2 ] = 0;
//            LogDebug("TX Packet contents: %s", dbgBuf);
//        }
//        /* Receive only case */
//        else if( usTxDataLen < usRxDataLen )
//        {
//
//            LogDebug( "Starting DMA RX. Offset: %d, Length: %d Bytes",
//                      usTxDataLen,
//                      ( usRxDataLen - usTxDataLen ) );
//
//            ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );
//
//            xHalStatus = HAL_SPI_Receive_DMA( pxCtx->pxSpiHandle,
//                                              &( pxRxBuffer[ usTxDataLen ] ),
//                                              ( usRxDataLen - usTxDataLen ) );
//
//        }
//        /* TX and RX were the same size. No action necessary */
//        else
//        {
//           xHalStatus = HAL_OK;
//        }
//    }
//
//    if( xHalStatus != HAL_OK )
//    {
//        LogError( "Error occurred during HAL_SPI_*_DMA transaction: %d", xHalStatus );
//    }
//    /* Wait for DMA event */
//    else
//    {
//        xHalStatus = ( xWaitForSPIEvent( MX_SPI_EVENT_TIMEOUT ) == pdTRUE ) ? HAL_OK : HAL_ERROR;
//    }
//
//    return ( xHalStatus == HAL_OK );
//}



static void vProcessRxPacket( MessageBufferHandle_t * xControlPlaneResponseBuff,
                              NetInterface_t * pxNetif,
                              PacketBuffer_t * * ppxRxPacket )
{
    BaseType_t xResult = pdFALSE;

    /* Read header */
    IPCHeader_t * pxRxPktHeader = ( IPCHeader_t * ) ( * ppxRxPacket )->payload;

    vPrintBuffer( (*ppxRxPacket)->payload, (*ppxRxPacket)->tot_len );

    /* Fast path: bypass in packets */
    if( pxRxPktHeader->usIPCApiId == IPC_WIFI_EVT_BYPASS_IN )
    {
        /* adjust header */
        pbuf_remove_header( ( *ppxRxPacket ), sizeof( BypassInOut_t ) );
        //TODO: Determine if this is needed.

        /* Enqueue */
        if( prvxLinkInput( pxNetif, *ppxRxPacket ) != pdTRUE )
        {
            /* Free packet on failure */
            PBUF_FREE( *ppxRxPacket );
        }
        /* Clear pointer */
        ( *ppxRxPacket ) = NULL;
    }
    /* Drop bypass out ACK packets */
    else if( pxRxPktHeader->usIPCApiId == IPC_WIFI_BYPASS_OUT )
    {
        IPCResponsBypassOut_t * pxBypassOutResponse = ( * ppxRxPacket )->payload + ( ptrdiff_t ) sizeof( IPCHeader_t );
        if( pxBypassOutResponse->status == IPC_SUCCESS )
        {
            LogDebug( "Dropping bypass response for request id: %d.", pxRxPktHeader->ulIPCRequestId );
        }
        else
        {
            LogError( "IPCError %d reported in bypass response for request ID: %d",
                      pxBypassOutResponse->status,
                      pxRxPktHeader->ulIPCRequestId );
        }

        PBUF_FREE( *ppxRxPacket );
        ( *ppxRxPacket ) = NULL;
        pxRxPktHeader = NULL;
    }
    /* forward to control plane handler */
    else
    {
        xResult = xMessageBufferSend( xControlPlaneResponseBuff,
                                      ppxRxPacket,
                                      sizeof( PacketBuffer_t * ),
                                      0 );

        if( xResult == pdFALSE )
        {
            LogError( "Error while adding received message to xControlPlaneResponseBuff: %d.", xResult );

            LogError( "Dropping response message: Request ID: %d, AppID: %d",
                      pxRxPktHeader->ulIPCRequestId,
                      pxRxPktHeader->usIPCApiId );
            PBUF_FREE( *ppxRxPacket );
            pxRxPktHeader = NULL;
        }

        /* Clear pointer */
        ( *ppxRxPacket ) = NULL;
    }
}


void vInitCallbacks( MxDataplaneCtx_t *pxCtx )
{
    HAL_StatusTypeDef xHalResult = HAL_ERROR;

    /* Register SPI and GPIO callback functions */
    GPIO_EXTI_Register_Callback( pxCtx->gpio_notify->xPinMask,
                                 spi_notify_callback,
                                 pxCtx );

    GPIO_EXTI_Register_Callback( pxCtx->gpio_flow->xPinMask,
                                 spi_flow_callback,
                                 pxCtx );




    xHalResult = HAL_SPI_RegisterCallback( pxCtx->pxSpiHandle,
                                           HAL_SPI_TX_COMPLETE_CB_ID,
                                           spi_transfer_done_callback );

    configASSERT( xHalResult == HAL_OK );

    xHalResult = HAL_SPI_RegisterCallback( pxCtx->pxSpiHandle,
                                           HAL_SPI_RX_COMPLETE_CB_ID,
                                           spi_transfer_done_callback );

    configASSERT( xHalResult == HAL_OK );

    xHalResult = HAL_SPI_RegisterCallback( pxCtx->pxSpiHandle,
                                           HAL_SPI_TX_RX_COMPLETE_CB_ID,
                                           spi_transfer_done_callback );

    configASSERT( xHalResult == HAL_OK );

    xHalResult = HAL_SPI_RegisterCallback( pxCtx->pxSpiHandle,
                                           HAL_SPI_ERROR_CB_ID,
                                           spi_transfer_error_callback );

    configASSERT( xHalResult == HAL_OK );
}

/* Wait for the flow pin go high signifying that the module is ready for more data. */
static inline BaseType_t xWaitForFlow( MxDataplaneCtx_t * pxCtx )
{
    uint32_t ulFlowValue = 0;

    /* Wait for flow pin to go high to signal that the module is ready */
    ulFlowValue = ulTaskNotifyTakeIndexed( SPI_EVT_FLOW_IDX, pdTRUE, MX_SPI_FLOW_TIMEOUT );

    if( ulFlowValue == 0 )
    {
        LogError( "Timed out while waiting for EVT_SPI_FLOW. ulFlowValue: %d, xTimeout: %d",
                ulFlowValue, MX_SPI_FLOW_TIMEOUT );
    }

    return( ( BaseType_t ) ( ulFlowValue != 0 ) );
}

void vDataplaneThread( void * pvParameters )
{
    /* Get context struct (contains instance parameters) */
    MxDataplaneCtx_t * pxCtx = ( MxDataplaneCtx_t * ) pvParameters;

    BaseType_t exitFlag = pdFALSE;

    /* Export context for callbacks */
    pxSpiCtx = pxCtx;

    vInitCallbacks( pxCtx );

    /* set CS/NSS high */
    vGpioSet( pxCtx->gpio_nss );

    /* Do hardware reset */
    vDoHardReset( pxCtx );

    while( exitFlag == pdFALSE )
    {
        PacketBuffer_t * pxTxBuff = NULL;
        PacketBuffer_t * pxRxBuff = NULL;

        LogDebug("Starting wait for DATA_WAITING_IDX event");
        ulTaskNotifyTakeIndexed( DATA_WAITING_IDX,
                                 pdFALSE,
                                 portMAX_DELAY );

        /* Get IRQ / notify pin state */
        BaseType_t xNotifyGpioLevel = xGpioGet( pxCtx->gpio_notify );

        /* Skip this transaction if no pending events */
        if( xNotifyGpioLevel == pdFALSE &&
            pxCtx->ulTxPacketsWaiting == 0 )
        {
            continue;
        }
        else if( xNotifyGpioLevel == pdTRUE )
        {
            LogDebug( "Performing read transaction." );
        }
        else if( pxCtx->ulTxPacketsWaiting > 0 )
        {
            LogDebug( "Performing write transaction." );
        }
        else
        {
            LogDebug( "Performing simultaneous Read/Write transaction" );
        }

        /* Clear flow state */
        xTaskNotifyStateClearIndexed( NULL, SPI_EVT_FLOW_IDX );

        /* Set CS low to initiate transaction */
        vGpioClear( pxCtx->gpio_nss );

        BaseType_t xResult = pdTRUE;

        /* Wait for the module to be ready */
        if( xWaitForFlow( pxCtx ) == pdTRUE )
        {
            uint16_t usTxLen = 0;
            uint16_t usRxLen = 0;

            QueueHandle_t xSourceQueue = NULL;

            /* If Notify pin is low, this is a TX transaction */
            if( xNotifyGpioLevel == pdFALSE )
            {
                /* Prepare a control plane messages for TX */
                if( xQueuePeek( pxCtx->xControlPlaneSendQueue, &pxTxBuff, 0 ) == pdTRUE )
                {
                    configASSERT( pxTxBuff != NULL );
                    configASSERT( pxTxBuff->ref > 0 );
                    usTxLen = pxTxBuff->tot_len;
                    xSourceQueue = pxCtx->xControlPlaneSendQueue;
                    LogDebug("Preparing controlplane message for transmission");
                }
                else if( xQueuePeek( pxCtx->xDataPlaneSendQueue, &pxTxBuff, 0 ) == pdTRUE )
                {
                    configASSERT( pxTxBuff != NULL );
                    configASSERT( pxTxBuff->ref > 0 );
                    usTxLen = pxTxBuff->tot_len;
                    xSourceQueue = pxCtx->xDataPlaneSendQueue;
                    LogDebug("Preparing dataplane message for transmission");
                }
                else
                {
                    /* Empty, no TX packets */
                }

                if( pxTxBuff == NULL &&
                    pxCtx->ulTxPacketsWaiting != 0 )
                {
                    LogWarn("Mismatch between ulTxPacketsWaiting and queue contents. Resetting ulTxPacketsWaiting");
                    pxSpiCtx->ulTxPacketsWaiting=0;
                }
            }

            if( xResult == pdTRUE )
            {
                /* Transfer the header */
                xResult = xDoSpiHeaderTransfer( pxCtx, &usTxLen, &usRxLen );
            }

            if( xResult == pdTRUE )
            {
                /* Allocate RX buffer */
                if( usRxLen > 0 )
                {
                    pxRxBuff = PBUF_ALLOC_RX( usRxLen );
                }
                /* Wait for flow pin to go high */
                xResult = xWaitForFlow( pxCtx );
            }

            /* Read from the queue */
            if( xResult == pdTRUE &&
                xSourceQueue != NULL )
            {
                xResult = xQueueReceive( xSourceQueue, &pxTxBuff, 0 );
                configASSERT( pxTxBuff != NULL );
                configASSERT( xResult == pdTRUE );
            }
            else if( pxTxBuff != NULL )
            {
                pxTxBuff = NULL;
            }

            /* Transmit / receive packet data */
            if( xResult == pdTRUE )
            {
                /* Transmit case */
                if( usTxLen > 0 &&
                    usRxLen ==0 )
                {
                    configASSERT( pxTxBuff );
                    xResult = xTransmitMessage( pxCtx, pxTxBuff->payload, usTxLen );
                }
                else if( usRxLen > 0 &&
                         usTxLen == 0 )
                {
                    configASSERT( pxRxBuff );
                    xResult = xReceiveMessage( pxCtx, pxRxBuff->payload, usRxLen );
                }
                else
                {
                    LogError( "usTxLen and usRxLen are both > 0. usTxLen: %d, usRxLen: %d", usTxLen, usRxLen );
                }
            }
        }
        else
        {
            LogDebug( "Timed out while waiting for flow event." );
            xResult = pdFALSE;
        }

        /* Set CS / NSS high (idle) */
        vGpioSet( pxCtx->gpio_nss );

        if( pxTxBuff != NULL )
        {
            /* Decrement TX packets waiting counter */
            ( void ) Atomic_Decrement_u32( &( pxSpiCtx->ulTxPacketsWaiting ) );

            /* Free the TX buffer */
            LogDebug( "Decreasing reference count of pxTxBuff %p from %d to %d", pxTxBuff, pxTxBuff->ref, ( pxTxBuff->ref - 1 ) );
            PBUF_FREE( pxTxBuff );
            pxTxBuff = NULL;
        }

        if( xResult == pdTRUE &&
            pxRxBuff != NULL )
        {
            vProcessRxPacket( pxCtx->xControlPlaneResponseBuff, pxCtx->pxNetif, &pxRxBuff );
        }
        else if( pxRxBuff != NULL )
        {
            LogDebug( "Decreasing reference count of pxRxBuff %p from %d to %d", pxRxBuff, pxRxBuff->ref, ( pxRxBuff->ref - 1 ) );
            PBUF_FREE( pxRxBuff );
            pxRxBuff = NULL;
        }

        configASSERT( pxTxBuff == NULL );
        configASSERT( pxRxBuff == NULL );


    }
}
