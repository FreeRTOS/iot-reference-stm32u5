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
#define LOG_LEVEL    LOG_ERROR
#include "logging.h"


#include "mx_ipc.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "message_buffer.h"
#include "netif/ethernet.h"
#include "string.h"
#include "atomic.h"
#include "lwip/pbuf.h"
#include "mx_prv.h"


/* Local types and enumerations */
typedef struct
{
    volatile uint32_t ulRequestID;
    PacketBuffer_t * pxTxPbuf;
    PacketBuffer_t * pxRxPbuf;
    TaskHandle_t xWaitingTask;
} IPCRequestCtx_t;

/* Static variables */
IPCRequestCtx_t xIPCRequestCtxArray[ NUM_IPC_REQUEST_CTX ];
static SemaphoreHandle_t xContextArrayMutex = NULL;     /* Mutex that must be held while modifying the xIPCRequestCtxArray */
static SemaphoreHandle_t xContextCountSemaphore = NULL; /* Allow clients to block while waiting for an IPCRequestCtx_t. */
static ControlPlaneCtx_t * pxControlPlaneCtx = NULL;

static void vClearCtx( IPCRequestCtx_t * pxRequestCtx )
{
    if( pxRequestCtx != NULL )
    {
        /* Acquire mutex before freeing the context */
        BaseType_t xResult;
        xResult = xSemaphoreTake( xContextArrayMutex,
                                  pdMS_TO_TICKS( MX_DEFAULT_TIMEOUT_MS ) );

        configASSERT( xResult == pdTRUE );

        /* Clear request ID to mark the context as available */
        pxRequestCtx->ulRequestID = 0;

        /* Free the request buffer pbuf and clear the pointer */
        if( pxRequestCtx->pxTxPbuf != NULL )
        {
            LogDebug( "Decreasing reference count of pbuf %p from %d to %d", pxRequestCtx->pxTxPbuf, pxRequestCtx->pxTxPbuf->ref, ( pxRequestCtx->pxTxPbuf->ref - 1 ) );
            PBUF_FREE( pxRequestCtx->pxTxPbuf );
            pxRequestCtx->pxTxPbuf = NULL;
        }

        /* Clear the handle of the waiting task */
        pxRequestCtx->xWaitingTask = NULL;

        /* Free the response buffer pbuf and clear the pointer */
        if( pxRequestCtx->pxRxPbuf != NULL )
        {
            LogDebug( "Decreasing reference count of pbuf %p from %d to %d", pxRequestCtx->pxRxPbuf, pxRequestCtx->pxRxPbuf->ref, ( pxRequestCtx->pxRxPbuf->ref - 1 ) );
            PBUF_FREE( pxRequestCtx->pxRxPbuf );
            pxRequestCtx->pxRxPbuf = NULL;
        }

        xResult = xSemaphoreGive( xContextArrayMutex );

        configASSERT( xResult == pdTRUE );

        /* Add a token the to counting semaphore */
        xResult = xSemaphoreGive( xContextCountSemaphore );

        configASSERT( xResult == pdTRUE );
    }
}

static IPCRequestCtx_t * pxFindAvailableCtx( TickType_t xTimeout,
                                             BaseType_t xPbufLen )
{
    IPCRequestCtx_t * pxRequestCtx = NULL;
    BaseType_t xResult = pdFALSE;

    configASSERT( xContextCountSemaphore != NULL );

    /* Wait for a context to become available, then take a token from xContextCountSemaphore */
    xResult = xSemaphoreTake( xContextCountSemaphore, xTimeout );

    configASSERT( xResult == pdTRUE );

    configASSERT( xContextArrayMutex != NULL );

    xResult = xSemaphoreTake( xContextArrayMutex, xTimeout );

    if( xResult == pdTRUE )
    {
        for( uint32_t i = 0; i < NUM_IPC_REQUEST_CTX; i++ )
        {
            if( xIPCRequestCtxArray[ i ].ulRequestID == 0 )
            {
                xIPCRequestCtxArray[ i ].ulRequestID = prvGetNextRequestID();

                pxRequestCtx = &( xIPCRequestCtxArray[ i ] );

                if( pxRequestCtx->pxRxPbuf != NULL )
                {
                    PBUF_FREE( pxRequestCtx->pxRxPbuf );
                    LogWarn( "pxRxPbuf for IPCRequestCtx %d was non-null upon re-use.", i );
                }

                if( pxRequestCtx->pxTxPbuf != NULL )
                {
                    PBUF_FREE( pxRequestCtx->pxTxPbuf );
                    LogWarn( "pxTxPbuf for IPCRequestCtx %d was non-null upon re-use.", i );
                }

                if( pxRequestCtx->xWaitingTask != NULL )
                {
                    pxRequestCtx->xWaitingTask = NULL;
                    LogWarn( "xWaitingTask for IPCRequestCtx %d was non-null upon re-use.", i );
                }

                /* Allocate a tx pbuf */
                pxRequestCtx->pxTxPbuf = PBUF_ALLOC_TX( xPbufLen );
/*                LogDebug( "Allocated pbuf: %p length: %d ref: %d",  pxRequestCtx->pxTxPbuf, pxRequestCtx->pxRxPbuf->tot_len, pxRequestCtx->pxRxPbuf->ref ); */
                break;
            }
        }

        xResult = xSemaphoreGive( xContextArrayMutex );

        configASSERT( xResult == pdTRUE );
    }
    else
    {
        LogError( "Timed out while acquiring xContextArrayMutex." );
    }

    return pxRequestCtx;
}

static IPCError_t xSendIPCRequest( IPCPacket_t * pxTxPkt,
                                   uint32_t ulTxPacketDataLen,
                                   void * pxResponse,
                                   uint32_t ulResponseLength,
                                   TickType_t xTimeout )
{
    IPCError_t xReturnValue = IPC_SUCCESS;

    /* Validate inputs */
    configASSERT( pxTxPkt != NULL );

    configASSERT( ulTxPacketDataLen <= sizeof( IPCPacketData_t ) );

    configASSERT( ( pxResponse != NULL && ulResponseLength > 0 ) ||
                  ( pxResponse == NULL && ulResponseLength == 0 ) );

    BaseType_t ulTxPacketLen = sizeof( IPCHeader_t ) + ulTxPacketDataLen;
    BaseType_t xResult = pdFALSE;

    /* Allocate a request context */
    IPCRequestCtx_t * pxRequestCtx = pxFindAvailableCtx( xTimeout, ulTxPacketLen );

    LogDebug( "Sending IPC packet with request_id: %d, api_id: %d, pktdatalen: %d, total_len: %d",
              pxRequestCtx->ulRequestID, pxTxPkt->xHeader.usIPCApiId, ulTxPacketDataLen, ulTxPacketLen );

    if( pxRequestCtx == NULL )
    {
        LogError( "Timed out while finding a request context." );
        xReturnValue = IPC_ERROR_INTERNAL;
    }
    else
    {
        /* Set request ID */
        pxTxPkt->xHeader.ulIPCRequestId = pxRequestCtx->ulRequestID;

        /* Set task handle */
        pxRequestCtx->xWaitingTask = xTaskGetCurrentTaskHandle();

        /* Copy to pbuf */
        ( void ) memcpy( pxRequestCtx->pxTxPbuf->payload, pxTxPkt, ulTxPacketLen );

        configASSERT( pxControlPlaneCtx->xControlPlaneSendQueue != NULL );

        /* Send to dataplane thread for transmission */
        xResult = xQueueSend( pxControlPlaneCtx->xControlPlaneSendQueue,
                              &( pxRequestCtx->pxTxPbuf ),
                              xTimeout );

        Atomic_Increment_u32( pxControlPlaneCtx->pulTxPacketsWaiting );

        if( xResult != pdTRUE )
        {
            LogError( "Error when sending message with request id=%d", pxRequestCtx->ulRequestID );
            xReturnValue = IPC_ERROR_INTERNAL;
        }
        else
        {
            /* Clear the pointer. Reference is now owned by the queue. */
            pxRequestCtx->pxTxPbuf = NULL;

            configASSERT( pxControlPlaneCtx->xDataPlaneTaskHandle != NULL );

            /* Notify dataplane thread of a waiting message */
            xTaskNotifyGiveIndexed( pxControlPlaneCtx->xDataPlaneTaskHandle, DATA_WAITING_IDX );
        }
    }

    IPCPacket_t * pxResponsePacket = NULL;

    /* If the message was sent successfully, wait for a task notification */
    if( xResult == pdTRUE )
    {
        /* Wait for notification */
        xResult = xTaskNotifyWait( 0, 0, NULL, xTimeout );

        if( xResult == pdTRUE )
        {
            pxResponsePacket = ( IPCPacket_t * ) pxRequestCtx->pxRxPbuf->payload;
        }
        else
        {
            xReturnValue = IPC_TIMEOUT;
        }

        pxRequestCtx->xWaitingTask = NULL;
    }

    if( ( pxResponsePacket != NULL ) &&
        ( ulResponseLength > 0 ) &&
        ( pxResponse != NULL ) )
    {
        ( void ) memcpy( pxResponse, &( pxResponsePacket->xData ), ulResponseLength );
    }

    /* Clear the context (also frees the response buffer) */
    vClearCtx( pxRequestCtx );

    return xReturnValue;
}

IPCError_t mx_RequestVersion( char * pcVersionBuffer,
                              uint32_t ulVersionLength,
                              TickType_t xTimeout )
{
    IPCError_t xReturnValue = IPC_SUCCESS;

    IPCPacket_t xTxPkt;

    xTxPkt.xHeader.usIPCApiId = IPC_SYS_VERSION;

    if( ( pcVersionBuffer != NULL ) &&
        ( ulVersionLength >= MX_FIRMWARE_REVISION_SIZE ) )
    {
        xReturnValue = xSendIPCRequest( &xTxPkt, 0,
                                        ( IPCPacketData_t * ) pcVersionBuffer,
                                        ulVersionLength,
                                        xTimeout );
    }
    else
    {
        xReturnValue = IPC_PARAMETER_ERROR;
    }

    return xReturnValue;
}

IPCError_t mx_FactoryReset( TickType_t xTimeout )
{
    IPCError_t xReturnValue = IPC_SUCCESS;

    IPCPacket_t xTxPkt;

    xTxPkt.xHeader.usIPCApiId = IPC_SYS_RESET;

    xReturnValue = xSendIPCRequest( &xTxPkt, 0,
                                    NULL, 0,
                                    xTimeout );
    return xReturnValue;
}

IPCError_t mx_GetMacAddress( MacAddress_t * pxMacAddress,
                             TickType_t xTimeout )
{
    IPCError_t xReturnValue = IPC_SUCCESS;

    if( pxMacAddress != NULL )
    {
        IPCPacket_t xTxPkt;
        xTxPkt.xHeader.usIPCApiId = IPC_WIFI_GET_MAC;

        xReturnValue = xSendIPCRequest( &xTxPkt,
                                        0,
                                        ( IPCPacketData_t * ) pxMacAddress,
                                        sizeof( struct eth_addr ),
                                        xTimeout );
    }
    else
    {
        xReturnValue = IPC_PARAMETER_ERROR;
    }

    return xReturnValue;
}

IPCError_t mx_Connect( const char * pcSSID,
                       const char * pcPSK,
                       TickType_t xTimeout )
{
    IPCError_t xReturnValue = IPC_SUCCESS;

    int32_t lPSKLength = 0;

    /* Validate parameters */

    if( ( pcSSID == NULL ) ||
        ( strnlen( pcSSID, MX_SSID_BUF_LEN ) >= MX_SSID_BUF_LEN ) )
    {
        LogError( "Invalid pcSSID parameter. pcSSID must be non-null and at most 32 characters long." );
        xReturnValue = IPC_PARAMETER_ERROR;
    }

    if( pcPSK == NULL )
    {
        LogError( "Invalid pcPSK parameter. pcPSK must be non-null." );
        xReturnValue = IPC_PARAMETER_ERROR;
    }
    else
    {
        lPSKLength = strnlen( pcPSK, MX_PSK_BUF_LEN );

        if( lPSKLength >= MX_PSK_BUF_LEN )
        {
            LogError( "Invalid pcSSID parameter. pcSSID must be 64 characters or less in length." );
            xReturnValue = IPC_PARAMETER_ERROR;
        }
    }

    if( xReturnValue == IPC_SUCCESS )
    {
        IPCPacket_t xTxPkt;

        xTxPkt.xHeader.usIPCApiId = IPC_WIFI_CONNECT;

        xTxPkt.xData.xRequestWifiConnect.ucUseAttr = pdFALSE;
        xTxPkt.xData.xRequestWifiConnect.ucUseStaticIp = pdFALSE;
        xTxPkt.xData.xRequestWifiConnect.ucAccessPointChannel = 0;
        xTxPkt.xData.xRequestWifiConnect.ucSecurityType = 0;

        ( void ) memset( &( xTxPkt.xData.xRequestWifiConnect.ucAccessPointBssid ),
                         0, MX_BSSID_LEN );
        ( void ) memset( &( xTxPkt.xData.xRequestWifiConnect.xStaticIpInfo ),
                         0, sizeof( IPInfoType_t ) );


        ( void ) strncpy( xTxPkt.xData.xRequestWifiConnect.cSSID,
                          pcSSID,
                          MX_SSID_BUF_LEN );

        xTxPkt.xData.xRequestWifiConnect.lKeyLength = lPSKLength;

        ( void ) strncpy( xTxPkt.xData.xRequestWifiConnect.cPSK,
                          pcPSK,
                          MX_PSK_BUF_LEN );


        xReturnValue = xSendIPCRequest( &xTxPkt,
                                        sizeof( IPCRequestWifiConnect_t ),
                                        NULL,
                                        0,
                                        xTimeout );
    }
    else
    {
        xReturnValue = IPC_PARAMETER_ERROR;
    }

    return xReturnValue;
}

IPCError_t mx_Disconnect( TickType_t xTimeout )
{
    IPCError_t xReturnValue = IPC_SUCCESS;

    IPCPacket_t xTxPkt;

    xTxPkt.xHeader.usIPCApiId = IPC_WIFI_DISCONNECT;

    xReturnValue = xSendIPCRequest( &xTxPkt, 0,
                                    NULL, 0,
                                    xTimeout );

    return xReturnValue;
}

IPCError_t mx_SetBypassMode( BaseType_t xEnable,
                             TickType_t xTimeout )
{
    IPCError_t xError = IPC_SUCCESS;

    IPCPacket_t xTxPkt;

    if( ( xEnable == pdFALSE ) ||
        ( xEnable == pdTRUE ) )
    {
        xTxPkt.xHeader.usIPCApiId = IPC_WIFI_BYPASS_SET;
        xTxPkt.xData.xRequestWifiBypassSet.enable = ( uint32_t ) xEnable;
        xError = xSendIPCRequest( &xTxPkt, sizeof( IPCRequestWifiBypassSet_t ),
                                  NULL, 0,
                                  xTimeout );
    }
    else
    {
        xError = IPC_PARAMETER_ERROR;
    }

    return xError;
}

IPCError_t mx_RegisterEventCallback( MxEventCallback_t xCallback,
                                     void * pxCallbackContext )
{
    IPCError_t xError;

    if( pxControlPlaneCtx != NULL )
    {
        xError = IPC_SUCCESS;
        pxControlPlaneCtx->xEventCallback = xCallback;
        pxControlPlaneCtx->pxEventCallbackCtx = pxCallbackContext;
    }
    else
    {
        LogError( "Unable to set MxEventCallback. ControlPlaneRouter task has not been started." );
        xError = IPC_ERROR_INTERNAL;
    }

    return xError;
}

/*
 * Serialize and pack control plane requests to module.
 * Block until callback has succeeded and is done with buffer
 * Needed for multiple concurrent control plane messages to be outstanding.
 */
void prvControlPlaneRouter( void * pvParameters )
{
    /* Get context */
    ControlPlaneCtx_t * pxCtx = ( ControlPlaneCtx_t * ) pvParameters;

    /* Export context to other functions in this file */
    pxControlPlaneCtx = pxCtx;

    xContextArrayMutex = xSemaphoreCreateMutex();

    xContextCountSemaphore = xSemaphoreCreateCounting( NUM_IPC_REQUEST_CTX, NUM_IPC_REQUEST_CTX );

    /* lock mutex, non-blocking */
    BaseType_t xResult = xSemaphoreTake( xContextArrayMutex, 0 );

    /* Mutex should always be free at this point */
    configASSERT( xResult == pdTRUE );

    for( uint32_t i = 0; i < NUM_IPC_REQUEST_CTX; i++ )
    {
        xIPCRequestCtxArray[ i ].ulRequestID = 0;
        xIPCRequestCtxArray[ i ].pxTxPbuf = NULL;
        xIPCRequestCtxArray[ i ].pxRxPbuf = NULL;
        xIPCRequestCtxArray[ i ].xWaitingTask = NULL;
    }

    xSemaphoreGive( xContextArrayMutex );

    while( 1 )
    {
        PacketBuffer_t * pxRxPbuf = NULL;

        /* Block on the input message queue */
        xResult = xMessageBufferReceive( pxCtx->xControlPlaneResponseBuff,
                                         &pxRxPbuf,
                                         sizeof( PacketBuffer_t * ),
                                         portMAX_DELAY );

        if( ( xResult != pdFALSE ) &&
            ( pxRxPbuf != NULL ) )
        {
            IPCPacket_t * pxRxPacket = ( IPCPacket_t * ) pxRxPbuf->payload;

            char ucPrintBuf[ pxRxPbuf->tot_len * 2 + 1 ];

            for( uint32_t i = 0; i < pxRxPbuf->tot_len; i++ )
            {
                snprintf( &ucPrintBuf[ 2 * i ], 3, "%02X", ( ( uint8_t * ) pxRxPbuf->payload )[ i ] );
            }

            ucPrintBuf[ pxRxPbuf->tot_len * 2 ] = 0;
            LogDebug( "%s", ucPrintBuf );

            /* Check if message is a notification */
            if( pxRxPacket->xHeader.ulIPCRequestId == 0 )
            {
                if( pxRxPacket->xHeader.usIPCApiId == IPC_WIFI_EVT_STATUS )
                {
                    pxCtx->xEventCallback( pxRxPacket->xData.xEventStatus.status, pxCtx->pxEventCallbackCtx );
                }
                else
                {
                    LogInfo( "Unhandled event message with AppID: %d",
                             pxRxPacket->xHeader.usIPCApiId );
                }
            }
            /* Otherwise, message is a response, find the relevant IPCRequestCtx to send the packet to */
            else
            {
                /* Wait for xContextArrayMutex */
                xResult = xSemaphoreTake( xContextArrayMutex, portMAX_DELAY );

                configASSERT( xResult == pdTRUE );

                IPCRequestCtx_t * pxTargetCtx = NULL;

                for( uint32_t i = 0; i < NUM_IPC_REQUEST_CTX; i++ )
                {
                    if( xIPCRequestCtxArray[ i ].ulRequestID == pxRxPacket->xHeader.ulIPCRequestId )
                    {
                        pxTargetCtx = &xIPCRequestCtxArray[ i ];
                        break;
                    }
                }

                /* Send packet to waiting thread */
                if( ( pxTargetCtx != NULL ) &&
                    ( pxTargetCtx->pxRxPbuf == NULL ) &&
                    ( pxTargetCtx->xWaitingTask != NULL ) )
                {
                    LogDebug( "Notifying waiting task %d of RX packet.", pxTargetCtx->xWaitingTask );
                    pxTargetCtx->pxRxPbuf = pxRxPbuf;
                    xResult = xTaskNotify( pxTargetCtx->xWaitingTask, 0, eNoAction );

                    if( xResult == pdTRUE )
                    {
                        /* Increase pbuf reference count */
                        pbuf_ref( pxRxPbuf );
                    }
                    else
                    {
                        pxTargetCtx->pxRxPbuf = NULL;
                        LogError( "Error delivering response message with AppId: %d and RequestId: %d",
                                  pxRxPacket->xHeader.usIPCApiId,
                                  pxRxPacket->xHeader.ulIPCRequestId );
                    }
                }
                else
                {
                    LogWarn( "Dropping response packet with AppId: %d and RequestId: %d",
                             pxRxPacket->xHeader.usIPCApiId,
                             pxRxPacket->xHeader.ulIPCRequestId );
                }

                /* Return the mutex */
                xResult = xSemaphoreGive( xContextArrayMutex );
                configASSERT( xResult == pdTRUE );
            }

            LogDebug( "Decreasing reference count of pxRxPbuf %p from %d to %d", pxRxPbuf, pxRxPbuf->ref, ( pxRxPbuf->ref - 1 ) );
            PBUF_FREE( pxRxPbuf );
        }
        else
        {
            LogError( "Error when reading from xControlPlaneResponseBuff" );
        }
    }
}
