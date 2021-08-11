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


#define EVT_SPI_DONE        0x8
#define EVT_SPI_ERROR       0x10

#define EVT_SPI_FLOW        0x2

#define DATA_WAITING_SNOTIFY 0x2

#define DATA_WAITING_IDX    0
#define SPI_EVT_DMA_IDX     1
#define SPI_EVT_FLOW_IDX    2

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
                                          eSetValueWithOverwrite,
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
                                          eSetValueWithOverwrite,
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
        /* Increment RX packets waiting counter */
        ( void ) Atomic_Increment_u32( &( pxSpiCtx->ulRxPacketsWaiting ) );

        rslt = xTaskNotifyIndexedFromISR( pxSpiCtx->xDataPlaneTaskHandle,
                                          DATA_WAITING_IDX,
                                          DATA_WAITING_SNOTIFY,
                                          eSetValueWithOverwrite,
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
                                          eSetValueWithOverwrite,
                                          &xHigherPriorityTaskWoken );
        configASSERT( rslt == pdTRUE );

        if( xHigherPriorityTaskWoken == pdTRUE )
        {
            vPortYield();
        }
    }
}

/* Handle GPIO operations */
static inline void mxfree_gpio_clear( const IotMappedPin_t * gpio )
{
    HAL_GPIO_WritePin( gpio->xPort, gpio->xPinMask, GPIO_PIN_RESET );
}

static inline void mxfree_gpio_set( const IotMappedPin_t * gpio )
{
    HAL_GPIO_WritePin( gpio->xPort, gpio->xPinMask, GPIO_PIN_SET );
}

static inline bool mxfree_gpio_get( const IotMappedPin_t * gpio )
{
    return ( bool ) HAL_GPIO_ReadPin( gpio->xPort,gpio->xPinMask );
}

static inline void mxfree_hard_reset( MxDataplaneCtx_t * pxCtx )
{
    HAL_GPIO_WritePin( pxCtx->gpio_reset->xPort, pxCtx->gpio_reset->xPinMask, GPIO_PIN_RESET );
    vTaskDelay(1000);
    HAL_GPIO_WritePin( pxCtx->gpio_reset->xPort, pxCtx->gpio_reset->xPinMask, GPIO_PIN_SET );
    vTaskDelay(12000);
}

/* SPI protocol definitions */
#define MX_SPI_WRITE        ( 0x0A )
#define MX_SPI_READ         ( 0x0B )

static inline BaseType_t xWaitForSPIEvent( TickType_t xTimeout )
{
    BaseType_t xWaitResult = pdFALSE;
    BaseType_t xReturnValue = pdFALSE;
    uint32_t ulNotifiedValue = 0;

    xWaitResult = xTaskNotifyWaitIndexed( SPI_EVT_DMA_IDX, 0, 0xFFFFFFFF, &ulNotifiedValue, xTimeout );

    if( xWaitResult == pdTRUE )
    {
        if( ulNotifiedValue & EVT_SPI_DONE )
        {
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
                                               uint16_t * pusTxLen,
                                               uint16_t * pusRxLen )
{
    HAL_StatusTypeDef xHalStatus = HAL_ERROR;

    SPIHeader_t xRxHeader = { 0 };
    SPIHeader_t xTxHeader = { 0 };

    xTxHeader.type = MX_SPI_WRITE;
    xTxHeader.len = *pusTxLen;
    xTxHeader.lenx = ~( xTxHeader.len );

    LogDebug( "Starting DMA transfer: type: %d, len: %d, lenx: %d",
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
       *pusRxLen = xRxHeader.len;
    }
    else
    {
        LogError( "Packet validation failed. len: %d, lenx: %d, xord: %d, type: %d, xHalStatus: %d",
                   xRxHeader.len, xRxHeader.lenx,  ( xRxHeader.len ) ^ ( xRxHeader.lenx ), xRxHeader.type, xHalStatus );
        *pusRxLen = 0;
        *pusTxLen = 0;
        xHalStatus = HAL_ERROR;
    }

   return( xHalStatus == HAL_OK );
}

static inline BaseType_t xTransmitPacket( MxDataplaneCtx_t * pxCtx,
                                          uint8_t * pxTxBuffer,
                                          uint8_t * pxRxBuffer,
                                          uint32_t usTxDataLen,
                                          uint32_t usRxDataLen,
                                          TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;

    /* Split into up to two dma transactions if both rx and tx ops are necessary */
    if( usTxDataLen > 0 &&
        usRxDataLen > 0 )
    {

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
            xHalStatus = ( xWaitForSPIEvent( xTimeout ) == pdTRUE ) ? HAL_OK : HAL_ERROR;
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
        xHalStatus = ( xWaitForSPIEvent( xTimeout ) == pdTRUE ) ? HAL_OK : HAL_ERROR;
    }

    return ( xHalStatus == HAL_OK );
}


/* Precondition Tx buffer must be properly sized */
static inline BaseType_t xDoSpiTransaction( MxDataplaneCtx_t * pxCtx,
                                            PacketBuffer_t * * ppxTxBuffer,
                                            PacketBuffer_t * * ppxRxBuffer,
                                            TickType_t xTimeout )
{
    BaseType_t xReturn = pdTRUE;

    uint16_t usTxLen = 0;
    uint16_t usRxLen = 0;

    /* Assert that the Rx Buffer pointer is null */
    configASSERT( *ppxRxBuffer == NULL );

    if( *ppxTxBuffer != NULL )
    {
        /* Assert that pxTxBuffer is contiguous */
       configASSERT( PBUF_VALID( *ppxTxBuffer ) );

       /* Set length to be placed in header */
       usTxLen = PBUF_LEN( *ppxTxBuffer );
    }

    (void) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_FLOW_IDX );

    /* Set CS low */
    mxfree_gpio_clear( pxCtx->gpio_nss );

    if( mxfree_gpio_get( pxCtx->gpio_flow ) == pdFALSE )
    {
        /* Wait for flow pin to go high to signal that the module is ready */
        if( ulTaskNotifyTakeIndexed( SPI_EVT_FLOW_IDX, pdTRUE, xTimeout ) == 0 )
        {
            LogError( "Timed out while waiting for EVT_SPI_FLOW. xTimeout=%d", MX_SPI_EVENT_TIMEOUT );
            xReturn = pdFALSE;
        }
        else
        {
            LogDebug( "Got EVT_SPI_FLOW" );
            xReturn = pdTRUE;
        }
    }

    (void) xTaskNotifyStateClearIndexed( NULL, SPI_EVT_FLOW_IDX );

    /* Exchange SPI headers with module */
    if( xReturn == pdTRUE )
    {
        xReturn = xDoSpiHeaderTransfer( pxCtx, &usTxLen, &usRxLen );
    }

    if( xReturn == pdTRUE )
    {
        /* Allocate RX buffer */
        if( usRxLen > 0 )
        {
            *ppxRxBuffer = PBUF_ALLOC_RX( usRxLen );
        }
    }
    else
    {
        LogError("An error occurred during spi header transfer.");
    }


    if( xReturn == pdTRUE )
    {
        if( mxfree_gpio_get( pxCtx->gpio_flow ) == pdFALSE )
        {
            /* Wait for flow pin to go high to signal that the module is ready to receive our data */
            if( ulTaskNotifyTakeIndexed( SPI_EVT_FLOW_IDX, pdTRUE, MX_SPI_EVENT_TIMEOUT ) == 0 )
            {
                LogError( "Timed out while waiting for EVT_SPI_FLOW. xTimeout=%d", MX_SPI_EVENT_TIMEOUT );
                xReturn = pdFALSE;
            }
            else
            {
                xReturn = pdTRUE;
            }
        }
    }

    /* Transmit / receive packet data */
    if( xReturn == pdTRUE )
    {
        xReturn = xTransmitPacket( pxCtx,
                                   *ppxTxBuffer ? ( *ppxTxBuffer )->payload : NULL,
                                   *ppxRxBuffer ? ( *ppxRxBuffer )->payload : NULL,
                                   usTxLen,
                                   usRxLen,
                                   xTimeout );
    }

    /* Set CS / NSS high to end transaction */
    mxfree_gpio_set( pxCtx->gpio_nss );

    return xReturn;
}

static void vAddMXHeaderToEthernetFrame( PacketBuffer_t * pxTxPacket )
{
    configASSERT( pxTxPacket != NULL );
    /* Store length of ethernet frame for BypassInOut_t header */
    uint16_t ulEthPacketLen = pxTxPacket->tot_len;

    /* Adjust pbuf size to include BypassInOut_t header */
    ( void ) pbuf_header( pxTxPacket, sizeof( BypassInOut_t ) );

    /* Add on bypass header */
    BypassInOut_t * pxBypassHeader = ( BypassInOut_t * ) pxTxPacket->payload;

    pxBypassHeader->xHeader.usIPCApiId = IPC_WIFI_BYPASS_OUT;
    pxBypassHeader->xHeader.ulIPCRequestId = prvGetNextRequestID();

    /* Send to station interface */
    pxBypassHeader->lIndex = WIFI_BYPASS_MODE_STATION;

    /* Fill pad region with zeros */
    ( void ) memset( pxBypassHeader->ucPad, 0, MX_BYPASS_PAD_LEN );

    /* Set length field */
    pxBypassHeader->usDataLen = ulEthPacketLen;
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

void vDataplaneThread( void * pvParameters )
{
    /* Get context struct (contains instance parameters) */
    MxDataplaneCtx_t * pxCtx = ( MxDataplaneCtx_t * ) pvParameters;

    BaseType_t exitFlag = pdFALSE;

    /* Export context for callbacks */
    pxSpiCtx = pxCtx;

    vInitCallbacks( pxCtx );

    /* Do hardware reset */
    mxfree_hard_reset( pxCtx );


    while( exitFlag == pdFALSE )
    {
        BaseType_t xResult = pdFALSE;
        PacketBuffer_t * pxTxPacket = NULL;
        PacketBuffer_t * pxRxPacket = NULL;

        /* Wait up to 100ms for an event if no packets are waiting */
        if( mxfree_gpio_get( pxCtx->gpio_notify ) == pdFALSE )
        {
            TickType_t xTimeout = 100;
            if( pxCtx->ulTxPacketsWaiting == 0 &&
                pxCtx->ulRxPacketsWaiting == 0 )
            {
                xTimeout = pdMS_TO_TICKS( 10 * 10000 );
            }

            ( void ) xTaskNotifyWaitIndexed( 0,
                                             0,
                                             0xFFFFFFFF,
                                             NULL,
                                             xTimeout );
        }

        /* Check for control plane messages to send */
        if( uxQueueMessagesWaiting( pxCtx->xControlPlaneSendQueue ) > 0 )
        {
            xResult = xQueueReceive( pxCtx->xControlPlaneSendQueue, &pxTxPacket, 0 );
            if( xResult != pdTRUE )
            {
                LogError( "Error reading from xControlPlaneSendQueue: %d", xResult );
            }
            configASSERT( pxTxPacket != NULL );
            configASSERT( pxTxPacket->ref > 0 );
            /* Note: No pbuf_header adjustment is necessary for control plane packets */
        }


        /* Otherwise, check for data plane messages to send */
        if( pxTxPacket == NULL &&
            xMessageBufferIsEmpty( pxCtx->xDataPlaneSendBuff ) == pdFALSE )
        {
            xResult = xMessageBufferReceive( pxCtx->xDataPlaneSendBuff, &pxTxPacket, sizeof( PacketBuffer_t * ), 0 );

            configASSERT( pxTxPacket != NULL );
            configASSERT( pxTxPacket->ref > 0 );

            if( xResult == pdTRUE )
            {
                vAddMXHeaderToEthernetFrame( pxTxPacket );
            }
            else
            {
                LogError( "Error reading from xDataPlaneSendBuff: %d", xResult );
            }
        }

        /* If either pointer is not null or an rx packet is waiting transfer some data */
        if( pxTxPacket != NULL ||
            pxRxPacket != NULL ||
            pxCtx->ulRxPacketsWaiting > 0 ||
            mxfree_gpio_get( pxCtx->gpio_notify ) == pdTRUE )
        {
            xResult = xDoSpiTransaction( pxCtx,
                                         &pxTxPacket,   /* Allocated elsewhere */
                                         &pxRxPacket,
                                         MX_SPI_TRANSACTION_TIMEOUT );

            /* Process further */
            if( pxTxPacket != NULL )
            {
                /* Decrement TX packets waiting counter */
                ( void ) Atomic_Decrement_u32( &( pxSpiCtx->ulTxPacketsWaiting ) );
                PBUF_FREE( pxTxPacket );
            }

            if( pxRxPacket != NULL )
            {
                /* Decrement RX packets waiting counter */
                ( void ) Atomic_Decrement_u32( &( pxSpiCtx->ulRxPacketsWaiting ) );

                vProcessRxPacket( pxCtx->xControlPlaneResponseBuff, pxCtx->pxNetif, &pxRxPacket );
            }
        }
    }
}


