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
#define LOG_LEVEL LOG_DEBUG

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


#define EVT_SPI_DONE        0x8
#define EVT_SPI_ERROR       0x10

#define EVT_SPI_FLOW        0x2

#define DATA_WAITING_SNOTIFY 0x20


#define SPI_EVT_DMA_IDX     1
#define SPI_EVT_FLOW_IDX    2
#define DATA_WAITING_IDX    3

static MxDataplaneCtx_t * pxSpiCtx = NULL;

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
static void spi_transfer_done_callback()
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

        if( xHigherPriorityTaskWoken == pdTRUE )
        {
            vPortYield();
        }
    }
}

static void spi_transfer_error_callback()
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

        if( xHigherPriorityTaskWoken == pdTRUE )
        {
            vPortYield();
        }
    }
}

/* Notify / IRQ pin transition means data is ready */
static void spi_notify_callback()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    BaseType_t rslt = pdFALSE;

    if( pxSpiCtx != NULL )
    {
        rslt = xTaskNotifyIndexedFromISR( pxSpiCtx->xDataPlaneTaskHandle,
                                          DATA_WAITING_IDX,
                                          DATA_WAITING_SNOTIFY,
                                          eSetBits,
                                          &xHigherPriorityTaskWoken );
        configASSERT( rslt == pdTRUE );

        if( xHigherPriorityTaskWoken == pdTRUE )
        {
            vPortYield();
        }
    }
}

static void spi_flow_callback()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    BaseType_t rslt = pdFALSE;

    if( pxSpiCtx != NULL )
    {
        rslt = xTaskNotifyIndexedFromISR( pxSpiCtx->xDataPlaneTaskHandle,
                                          SPI_EVT_FLOW_IDX,
                                          EVT_SPI_FLOW,
                                          eSetBits,
                                          &xHigherPriorityTaskWoken );
        configASSERT( rslt == pdTRUE );

        if( xHigherPriorityTaskWoken == pdTRUE )
        {
            vPortYield();
        }
    }
}

/* Handle GPIO operations */
static inline void mx_gpio_clear( const IotMappedPin_t * gpio )
{
    HAL_GPIO_WritePin( gpio->xPort, gpio->xPinMask, GPIO_PIN_RESET );
}

static inline void mx_gpio_set( const IotMappedPin_t * gpio )
{
    HAL_GPIO_WritePin( gpio->xPort, gpio->xPinMask, GPIO_PIN_SET );
}

static inline BaseType_t mx_gpio_get( const IotMappedPin_t * gpio )
{
    return (BaseType_t) HAL_GPIO_ReadPin( gpio->xPort,gpio->xPinMask );
}

static inline void mx_hard_reset( MxDataplaneCtx_t * pxCtx )
{
    HAL_GPIO_WritePin( pxCtx->gpio_reset->xPort, pxCtx->gpio_reset->xPinMask, GPIO_PIN_RESET );
    vTaskDelay(100);
    HAL_GPIO_WritePin( pxCtx->gpio_reset->xPort, pxCtx->gpio_reset->xPinMask, GPIO_PIN_SET );
    vTaskDelay(1200);
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
            LogDebug("SPI DMA DONE event received.");
            xReturnValue = pdTRUE;
        }

        if( ulNotifiedValue & EVT_SPI_ERROR )
        {
            LogError( "SPI DMA Error event received." );
            xReturnValue = pdFALSE;
        }

        if( ( ulNotifiedValue & ( EVT_SPI_ERROR | EVT_SPI_DONE ) ) == 0 )
        {
            LogError( "Timeout while waiting for SPI Event." );
            xReturnValue = pdFALSE;
        }
    }
    return xReturnValue;
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

    /* Write or simultaneous Read and Write operation */
    if( *psTxLen > 0 )
    {
        xTxHeader.type = MX_SPI_WRITE;
        xTxHeader.len = *psTxLen;
        xTxHeader.lenx = ~( xTxHeader.len );
    }
    /* Read operation only */
    else
    {
        xTxHeader.type = MX_SPI_READ;
        xTxHeader.len = 0;
        xTxHeader.lenx = 0xFFFF; //TODO: Does this need to be a particular value?
    }

    LogDebug( "Starting DMA header transfer: type: %d, len: %d, lenx: %d",
              xTxHeader.type, xTxHeader.len, xTxHeader.lenx );
    LogDebug( "TxHeader: %02x %02x %02x %02x %02x %02x %02x %02x sz: %d",
              ((char *)&xTxHeader)[0],
              ((char *)&xTxHeader)[1],
              ((char *)&xTxHeader)[2],
              ((char *)&xTxHeader)[3],
              ((char *)&xTxHeader)[4],
              ((char *)&xTxHeader)[5],
              ((char *)&xTxHeader)[6],
              ((char *)&xTxHeader)[7],
              sizeof( SPIHeader_t ) );

    ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );

    xHalStatus = HAL_SPI_TransmitReceive_DMA( pxCtx->pxSpiHandle,
                                              ( uint8_t * ) &xTxHeader,
                                              ( uint8_t * ) &xRxHeader,
                                              sizeof( SPIHeader_t ) );

   /* Exchange headers with the module */
   if( xHalStatus != HAL_OK )
   {
       LogError( "Error returned by HAL_SPI_TransmitReceive_DMA call during inital transfer. xHalStatus=%d", xHalStatus );
   }
   else
   {
       xHalStatus = ( xWaitForSPIEvent( MX_SPI_EVENT_TIMEOUT ) == pdTRUE ) ? HAL_OK : HAL_ERROR;
   }

   LogDebug( "RxHeader: %02x %02x %02x %02x %02x %02x %02x %02x sz: %d",
             ((char *)&xRxHeader)[0],
             ((char *)&xRxHeader)[1],
             ((char *)&xRxHeader)[2],
             ((char *)&xRxHeader)[3],
             ((char *)&xRxHeader)[4],
             ((char *)&xRxHeader)[5],
             ((char *)&xRxHeader)[6],
             ((char *)&xRxHeader)[7],
             sizeof( SPIHeader_t ) );


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
            LogError( "RX packet validation failed. len: %d, lenx: %d, xord: %d, type: %d, xHalStatus: %d",
                       xRxHeader.len, xRxHeader.lenx,  ( xRxHeader.len ) ^ ( xRxHeader.lenx ), xRxHeader.type, xHalStatus );
        }
        *psRxLen = 0;
    }

   return( xHalStatus == HAL_OK );
}

static inline BaseType_t xTransmitPacket( MxDataplaneCtx_t * pxCtx,
                                          uint8_t * pxTxBuffer,
                                          uint8_t * pxRxBuffer,
                                          uint32_t usTxDataLen,
                                          uint32_t usRxDataLen )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;

    LogDebug("Entering xTransmitPacket");

    /* Split into up to two dma transactions if both rx and tx ops are necessary */
    if( usTxDataLen > 0 &&
        usRxDataLen > 0 )
    {
        LogDebug("Starting simultaneous transmit");
        /* Perform TXRX transaction for common data length */
        if( usTxDataLen > usRxDataLen )
        {
            LogDebug( "TXRX %d Bytes", usRxDataLen );

            ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );

            xHalStatus = HAL_SPI_TransmitReceive_DMA( pxCtx->pxSpiHandle,
                                                      pxTxBuffer,
                                                      pxRxBuffer,
                                                      usRxDataLen );

        }
        else if( usRxDataLen > usTxDataLen )
        {
            LogDebug( "TXRX %d Bytes", usTxDataLen );

            ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );

            xHalStatus = HAL_SPI_TransmitReceive_DMA( pxCtx->pxSpiHandle,
                                                      pxTxBuffer,
                                                      pxRxBuffer,
                                                      usTxDataLen );

        }
        else
        {
            /* Empty */
        }

        if( xHalStatus == HAL_OK )
        {
            /* Wait for DMA event */
            xHalStatus = ( xWaitForSPIEvent( MX_SPI_EVENT_TIMEOUT ) == pdTRUE ) ? HAL_OK : HAL_ERROR;
        }
        else
        {
            LogError( "Error occurred during HAL_SPI_TransmitReceive_DMA transaction: %d", xHalStatus );
        }
    }

    if( xHalStatus == HAL_OK )
    {
        /* Transmit only case */
        if( usTxDataLen > usRxDataLen )
        {
            LogDebug( "Starting DMA TX. Offset: %d, Length: %d Bytes",
                      usRxDataLen,
                      ( usTxDataLen - usRxDataLen ) );

            ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );

            xHalStatus = HAL_SPI_Transmit_DMA( pxCtx->pxSpiHandle,
                                               &( pxTxBuffer[ usRxDataLen ] ),
                                               ( usTxDataLen - usRxDataLen ) );
            char dbgBuf[ usTxDataLen * 2 + 1 ];
            for( uint32_t i = 0; i < usTxDataLen; i++ )
            {
                snprintf(&dbgBuf[2*i],3,"%02X",pxTxBuffer[i]);
            }
            dbgBuf[ usTxDataLen * 2 ] = 0;
            LogDebug("TX Packet contents: %s", dbgBuf);
        }
        /* Receive only case */
        else if( usTxDataLen < usRxDataLen )
        {

            LogDebug( "Starting DMA RX. Offset: %d, Length: %d Bytes",
                      usTxDataLen,
                      ( usRxDataLen - usTxDataLen ) );

            ( void ) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_DMA_IDX );

            xHalStatus = HAL_SPI_Receive_DMA( pxCtx->pxSpiHandle,
                                              &( pxRxBuffer[ usTxDataLen ] ),
                                              ( usRxDataLen - usTxDataLen ) );

        }
        /* TX and RX were the same size. No action necessary */
        else
        {
           xHalStatus = HAL_OK;
        }
    }

    if( xHalStatus != HAL_OK )
    {
        LogError( "Error occurred during HAL_SPI_*_DMA transaction: %d", xHalStatus );
    }
    /* Wait for DMA event */
    else
    {
        xHalStatus = ( xWaitForSPIEvent( MX_SPI_EVENT_TIMEOUT ) == pdTRUE ) ? HAL_OK : HAL_ERROR;
    }

    return ( xHalStatus == HAL_OK );
}



static void vProcessRxPacket( MessageBufferHandle_t * xControlPlaneResponseBuff,
                              NetInterface_t * pxNetif,
                              PacketBuffer_t * * ppxRxPacket )
{
    BaseType_t xResult = pdFALSE;

    /* Read header */
    IPCHeader_t * pxRxPktHeader = ( IPCHeader_t * ) ( * ppxRxPacket )->payload;

    /* Fast path: bypass in packets */
    if( pxRxPktHeader->usIPCApiId == IPC_WIFI_EVT_BYPASS_IN )
    {
        /* adjust header */
//        pbuf_remove_header( ( *ppxRxPacket ), sizeof( BypassInOut_t ) );
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

/* Wait forever for a flow event */
static inline BaseType_t xWaitForFlow( MxDataplaneCtx_t * pxCtx )
{
    BaseType_t xReturnValue = pdFALSE;

    LogDebug("Entered xWaitForFlow");

    xReturnValue = mx_gpio_get( pxCtx->gpio_flow );

    /* If flow line is low, block until it changes state or a timeout occurs. */
    if( xReturnValue == pdFALSE )
    {
        uint32_t ulFlowValue = 0;
        LogDebug("Starting wait for FLOW signal. xTimeout=%d", MX_SPI_FLOW_TIMEOUT );

        /* Wait for flow pin to go high to signal that the module is ready */
        ulFlowValue = ulTaskNotifyTakeIndexed( SPI_EVT_FLOW_IDX, pdTRUE, MX_SPI_FLOW_TIMEOUT );

        if( ulFlowValue & EVT_SPI_FLOW )
        {
            xReturnValue = pdTRUE;
        }
        else
        {
            LogError( "Timed out while waiting for EVT_SPI_FLOW. ulFlowValue: %d, xTimeout: %d", ulFlowValue, MX_SPI_FLOW_TIMEOUT );
            xReturnValue = pdFALSE;
        }
        LogDebug( "FLOW signal wait complete. ulFlowValue=%d", ulFlowValue );
    }
    else
    {
        /* Clear notification */
        (void) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_FLOW_IDX );
    }

    return xReturnValue;
}

void vDataplaneThread( void * pvParameters )
{
    /* Get context struct (contains instance parameters) */
    MxDataplaneCtx_t * pxCtx = ( MxDataplaneCtx_t * ) pvParameters;

    BaseType_t exitFlag = pdFALSE;

    /* Export context for callbacks */
    pxSpiCtx = pxCtx;

    vInitCallbacks( pxCtx );

    /* Do hardware reset */
    mx_hard_reset( pxCtx );

    vTaskDelay(10 * 1000);

    while( exitFlag == pdFALSE )
    {
        PacketBuffer_t * pxTxBuff = NULL;
        PacketBuffer_t * pxRxBuff = NULL;
        uint32_t ulNotificationValue = 0;

        /* Block on task notification if waiting packet counter is 0 */
        if( pxCtx->ulTxPacketsWaiting == 0 )
        {
            xTaskNotifyStateClearIndexed( NULL, DATA_WAITING_IDX );
            BaseType_t xNotifyRslt = xTaskNotifyWaitIndexed( DATA_WAITING_IDX,
                                             0,
                                             0,
                                             &ulNotificationValue,
                                             5*1000 );

            /* Got notify event from IRQ pin, next transaction should include a read */
            if( ( ulNotificationValue & DATA_WAITING_SNOTIFY ) > 0 )
            {
                LogDebug("Woke up due to NOTIFY event.");
            }
            else
            {
                LogDebug("woke up for unknown reason. NOTIFY: %d, ulNotificationValue: %d, xNotifyRslt: %d", mx_gpio_get( pxCtx->gpio_notify ), ulNotificationValue, xNotifyRslt );
            }

            LogDebug("NOTIFY STATE: %d",  mx_gpio_get( pxCtx->gpio_notify ) );

        }

        if( pxCtx->ulTxPacketsWaiting == 0 &&
            mx_gpio_get( pxCtx->gpio_notify ) == pdFALSE )
        {
            LogWarn( "Reached transaction start with nothing to do." );
//            continue;
        }

        /* Set CS low to initiate transaction */
        mx_gpio_clear( pxCtx->gpio_nss );

        /* Wait for the module to be ready */
        if( xWaitForFlow( pxCtx ) == pdTRUE )
        {
            uint16_t usTxLen = 0;
            uint16_t usRxLen = 0;
            BaseType_t xResult = pdTRUE;

            QueueHandle_t xSourceQueue = NULL;

            /* Prepare a control plane messages for TX */
            if( xQueuePeek( pxCtx->xControlPlaneSendQueue, &pxTxBuff, 0 ) == pdTRUE )
            {
                LogDebug("Dequeued control plane message from xControlPlaneSendQueue");
                configASSERT( pxTxBuff != NULL );
                configASSERT( pxTxBuff->ref > 0 );
                usTxLen = pxTxBuff->tot_len;
                xSourceQueue = pxCtx->xControlPlaneSendQueue;
            }
            else if( xQueuePeek( pxCtx->xDataPlaneSendQueue, &pxTxBuff, 0 ) == pdTRUE )
            {
                configASSERT( pxTxBuff != NULL );
                configASSERT( pxTxBuff->ref > 0 );
                usTxLen = pxTxBuff->tot_len;
                xSourceQueue = pxCtx->xDataPlaneSendQueue;
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

            if( pxTxBuff == NULL &&
                mx_gpio_get( pxCtx->gpio_notify ) == pdFALSE )
            {
                LogWarn( "Reached xDoSpiHeaderTransfer with nothing to do. Resetting ulRxPacketsWaiting." );
                pxCtx->ulRxPacketsWaiting = 0;
                xResult = pdFALSE;
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
                /* Wait for flow pin to go high again */
                xResult = xWaitForFlow( pxCtx );
            }

            /* Read from the queue now that the module has ACKd */
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
                xResult = xTransmitPacket( pxCtx,
                                           (usTxLen>0) ? pxTxBuff->payload : NULL,
                                           (usRxLen>0) ? pxRxBuff->payload : NULL,
                                           usTxLen,
                                           usRxLen );
            }
        }
        else
        {
            LogDebug( "Timed out while waiting for flow event." );
        }

        /* Set CS / NSS high to end transaction */
        mx_gpio_set( pxCtx->gpio_nss );

        /* Free TX buffer */
        if( pxTxBuff != NULL )
        {
            /* Decrement TX packets waiting counter */
            ( void ) Atomic_Decrement_u32( &( pxSpiCtx->ulTxPacketsWaiting ) );
            PBUF_FREE( pxTxBuff );
            pxTxBuff = NULL;
        }

        if( pxRxBuff != NULL )
        {
            /* Decrement RX packets waiting counter */
            ( void ) Atomic_Decrement_u32( &( pxSpiCtx->ulRxPacketsWaiting ) );
            vProcessRxPacket( pxCtx->xControlPlaneResponseBuff, pxCtx->pxNetif, &pxRxBuff );
        }

        configASSERT( pxTxBuff == NULL );
        configASSERT( pxRxBuff == NULL );
    }
}
