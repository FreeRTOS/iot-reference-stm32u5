/*
 * FreeRTOS STM32 Reference Integration
 *
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
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#include "logging_levels.h"
#define LOG_LEVEL    LOG_ERROR
#include "logging.h"

/* Standard includes */
#include <stdint.h>
#include <limits.h>

#include "mx_netconn.h"
#include "mx_lwip.h"
#include "mx_prv.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "kvstore.h"
#include "hw_defs.h"

/* lwip includes */
#include "lwip/tcpip.h"
#include "lwip/netifapi.h"
#include "lwip/prot/dhcp.h"
#include "lwip/apps/lwiperf.h"

#include "sys_evt.h"

#include "stm32u5_iot_board.h"

#define MACADDR_RETRY_WAIT_TIME_TICKS    pdMS_TO_TICKS( 10 * 1000 )

static TaskHandle_t xNetTaskHandle = NULL;
static MxDataplaneCtx_t xDataPlaneCtx;
static ControlPlaneCtx_t xControlPlaneCtx;

#if LOG_LEVEL == LOG_DEBUG

/*
 * @brief Converts from a MxEvent_t to a C string.
 */
static const char * pcMxStatusToString( MxStatus_t xStatus )
{
    const char * pcReturn = "Unknown";

    switch( xStatus )
    {
        case MX_STATUS_NONE:
            pcReturn = "None";
            break;

        case MX_STATUS_STA_DOWN:
            pcReturn = "Station Down";
            break;

        case MX_STATUS_STA_UP:
            pcReturn = "Station Up";
            break;

        case MX_STATUS_STA_GOT_IP:
            pcReturn = "Station Got IP";
            break;

        case MX_STATUS_AP_DOWN:
            pcReturn = "AP Down";
            break;

        case MX_STATUS_AP_UP:
            pcReturn = "AP Up";
            break;

        default:
            /* default to "Unknown" string */
            break;
    }

    return pcReturn;
}
#endif /* if LOG_LEVEL == LOG_DEBUG */

/* Wait for all bits in ulTargetBits */
static uint32_t ulWaitForNotifyBits( BaseType_t uxIndexToWaitOn,
                                     uint32_t ulTargetBits,
                                     TickType_t xTicksToWait )
{
    TickType_t xRemainingTicks = xTicksToWait;
    TimeOut_t xTimeOut;

    vTaskSetTimeOutState( &xTimeOut );

    uint32_t ulNotifyValueAccumulate = 0x0;

    while( ( ulNotifyValueAccumulate & ulTargetBits ) != ulTargetBits )
    {
        uint32_t ulNotifyValue = 0x0;
        ( void ) xTaskNotifyWaitIndexed( uxIndexToWaitOn,
                                         0x0,
                                         ulTargetBits, /* Clear only the target bits on return */
                                         &ulNotifyValue,
                                         xRemainingTicks );

        /* Accumulate notification bits */
        if( ulNotifyValue > 0 )
        {
            ulNotifyValueAccumulate |= ulNotifyValue;
        }

        /* xTaskCheckForTimeOut adjusts xRemainingTicks */
        if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTicks ) == pdTRUE )
        {
            /* Timed out. Exit loop */
            break;
        }
    }

    /* Check for other event bits received */
    if( ( ulNotifyValueAccumulate & ( ~ulTargetBits ) ) > 0 )
    {
        /* send additional notification so these events are not lost */
        ( void ) xTaskNotifyIndexed( xTaskGetCurrentTaskHandle(),
                                     uxIndexToWaitOn,
                                     0,
                                     eNoAction );
    }

    return( ( ulTargetBits & ulNotifyValueAccumulate ) > 0 );
}

static void vHandleMxStatusUpdate( MxNetConnectCtx_t * pxCtx )
{
    if( pxCtx->xStatus != pxCtx->xStatusPrevious )
    {
        switch( pxCtx->xStatus )
        {
            case MX_STATUS_STA_UP:
            case MX_STATUS_STA_GOT_IP:
            case MX_STATUS_AP_UP:
                /* Set link up */
                vSetLinkUp( &( pxCtx->xNetif ) );
                break;

            case MX_STATUS_NONE:
            case MX_STATUS_STA_DOWN:
            case MX_STATUS_AP_DOWN:
                vSetLinkDown( &( pxCtx->xNetif ) );
                break;

            default:
                LogWarn( "Unknown mxchip status indication: %d", pxCtx->xStatus );
                /* Fail safe to setting link up */
                vSetLinkUp( &( pxCtx->xNetif ) );
                break;
        }
    }
}

static BaseType_t xWaitForMxStatus( MxNetConnectCtx_t * pxCtx,
                                    MxStatus_t xTargetStatus,
                                    TickType_t xTicksToWait )
{
    TickType_t xRemainingTicks = xTicksToWait;
    TimeOut_t xTimeOut;

    if( pxCtx->xStatus == xTargetStatus )
    {
        return pdTRUE;
    }

    vTaskSetTimeOutState( &xTimeOut );

    uint32_t ulNotifyBits;

    while( pxCtx->xStatus <= xTargetStatus )
    {
        ulNotifyBits = ulWaitForNotifyBits( NET_EVT_IDX,
                                            MX_STATUS_UPDATE_BIT,
                                            xRemainingTicks );

        /* xTaskCheckForTimeOut adjusts xRemainingTicks */
        if( ( ulNotifyBits > 0 ) ||
            ( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTicks ) == pdTRUE ) )
        {
            /* Timed out or success. Exit loop */
            break;
        }
    }

    return( pxCtx->xStatus >= xTargetStatus );
}

BaseType_t net_request_reconnect( void )
{
    BaseType_t xReturn = pdFALSE;

    LogDebug( "net_request_reconnect" );

    if( xNetTaskHandle != NULL )
    {
        xReturn = xTaskNotifyIndexed( xNetTaskHandle,
                                      NET_EVT_IDX,
                                      ASYNC_REQUEST_RECONNECT_BIT,
                                      eSetBits );
    }

    return xReturn;
}

/*
 * Handles network interface state change notifications from the control plane.
 */
static void vMxStatusNotify( MxStatus_t xNewStatus,
                             void * pvCtx )
{
    MxNetConnectCtx_t * pxCtx = ( MxNetConnectCtx_t * ) pvCtx;

    pxCtx->xStatusPrevious = pxCtx->xStatus;
    pxCtx->xStatus = xNewStatus;

    LogDebug( "Mx Status notification: %s -> %s ",
              pcMxStatusToString( pxCtx->xStatusPrevious ),
              pcMxStatusToString( pxCtx->xStatus ) );

    vHandleMxStatusUpdate( pxCtx );

    ( void ) xTaskNotifyIndexed( pxCtx->xNetTaskHandle,
                                 NET_EVT_IDX,
                                 MX_STATUS_UPDATE_BIT,
                                 eSetBits );
}

static void vLwipReadyCallback( void * pvCtx )
{
    MxNetConnectCtx_t * pxCtx = ( MxNetConnectCtx_t * ) pvCtx;

    if( xNetTaskHandle != NULL )
    {
        ( void ) xTaskNotifyIndexed( pxCtx->xNetTaskHandle,
                                     NET_EVT_IDX,
                                     NET_LWIP_READY_BIT,
                                     eSetBits );
    }
}

static char pcSSID[ MX_SSID_BUF_LEN ] = { 0 };
static char pcPSK[ MX_PSK_BUF_LEN ] = { 0 };

static BaseType_t xConnectToAP( MxNetConnectCtx_t * pxCtx )
{
    IPCError_t xErr = IPC_SUCCESS;


    if( ( pxCtx->xStatus == MX_STATUS_NONE ) ||
        ( pxCtx->xStatus == MX_STATUS_STA_DOWN ) )
    {
        xErr |= mx_SetBypassMode( pdTRUE,
                                  pdMS_TO_TICKS( MX_DEFAULT_TIMEOUT_MS ) );

        ( void ) KVStore_getString( CS_WIFI_SSID, pcSSID, MX_SSID_BUF_LEN );
        ( void ) KVStore_getString( CS_WIFI_CREDENTIAL, pcPSK, MX_PSK_BUF_LEN );

        xErr = mx_Connect( pcSSID, pcPSK, MX_TIMEOUT_CONNECT );

        /* Clear sensitive data */
        memset( pcSSID, 0, MX_SSID_BUF_LEN );
        memset( pcPSK, 0, MX_SSID_BUF_LEN );

        if( xErr != IPC_SUCCESS )
        {
            LogError( "Failed to connect to access point." );
        }
        else
        {
            ( void ) xWaitForMxStatus( pxCtx, MX_STATUS_STA_UP, MX_TIMEOUT_CONNECT );
        }
    }

    return( pxCtx->xStatus >= MX_STATUS_STA_UP );
}

static void vInitializeWifiModule( MxNetConnectCtx_t * pxCtx )
{
    IPCError_t xErr = IPC_ERROR_INTERNAL;

    vTaskDelay( 5000 );

    while( xErr != IPC_SUCCESS )
    {
        /* Query mac address and firmware revision */
        xErr = mx_RequestVersion( pxCtx->pcFirmwareRevision, MX_FIRMWARE_REVISION_SIZE, 1000 );

        /* Ensure null termination */
        pxCtx->pcFirmwareRevision[ MX_FIRMWARE_REVISION_SIZE ] = '\0';

        if( xErr != IPC_SUCCESS )
        {
            LogError( "Error while querying module firmware revision." );
        }

        if( xErr == IPC_SUCCESS )
        {
            /* Request mac address */
            xErr = mx_GetMacAddress( &( pxCtx->xMacAddress ), 1000 );

            if( xErr != IPC_SUCCESS )
            {
                LogError( "Error while querying wifi module mac address." );
            }
        }

        if( xErr != IPC_SUCCESS )
        {
            vTaskDelay( MACADDR_RETRY_WAIT_TIME_TICKS );
        }
        else
        {
            LogInfo( "Firmware Version:   %s", pxCtx->pcFirmwareRevision );
            LogInfo( "HW Address:         %02X:%02X:%02X:%02X:%02X:%02X",
                     pxCtx->xMacAddress.addr[ 0 ], pxCtx->xMacAddress.addr[ 1 ],
                     pxCtx->xMacAddress.addr[ 2 ], pxCtx->xMacAddress.addr[ 3 ],
                     pxCtx->xMacAddress.addr[ 4 ], pxCtx->xMacAddress.addr[ 5 ] );
        }
    }
}

static void vInitializeContexts( MxNetConnectCtx_t * pxCtx )
{
    MessageBufferHandle_t xControlPlaneResponseBuff;
    QueueHandle_t xControlPlaneSendQueue;
    QueueHandle_t xDataPlaneSendQueue;

    /* Construct queues */
    xDataPlaneSendQueue = xQueueCreate( DATA_PLANE_QUEUE_LEN, sizeof( PacketBuffer_t * ) );
    configASSERT( xDataPlaneSendQueue != NULL );

    xControlPlaneResponseBuff = xMessageBufferCreate( CONTROL_PLANE_BUFFER_SZ );
    configASSERT( xControlPlaneResponseBuff != NULL );

    xControlPlaneSendQueue = xQueueCreate( CONTROL_PLANE_QUEUE_LEN, sizeof( PacketBuffer_t * ) );
    configASSERT( xControlPlaneSendQueue != NULL );


    /* Initialize wifi connect context */
    pxCtx->xStatus = MX_STATUS_NONE;

    ( void ) memset( &( pxCtx->pcFirmwareRevision ), 0, MX_FIRMWARE_REVISION_SIZE );
    ( void ) memset( &( pxCtx->xMacAddress ), 0, sizeof( MacAddress_t ) );

    pxCtx->xDataPlaneSendQueue = xDataPlaneSendQueue;
    pxCtx->pulTxPacketsWaiting = &( xDataPlaneCtx.ulTxPacketsWaiting );
    pxCtx->xNetTaskHandle = xTaskGetCurrentTaskHandle();

    /* Construct dataplane context */

    /* Initialize GPIO pins map / handles */
    xDataPlaneCtx.gpio_flow = &( xGpioMap[ GPIO_MX_FLOW ] );
    xDataPlaneCtx.gpio_reset = &( xGpioMap[ GPIO_MX_RESET ] );
    xDataPlaneCtx.gpio_nss = &( xGpioMap[ GPIO_MX_NSS ] );
    xDataPlaneCtx.gpio_notify = &( xGpioMap[ GPIO_MX_NOTIFY ] );

    xDataPlaneCtx.pxSpiHandle = pxHndlSpi2;

    /* Initialize waiting packet counters */
    xDataPlaneCtx.ulTxPacketsWaiting = 0;

    /* Set queue handles */
    xDataPlaneCtx.xControlPlaneSendQueue = xControlPlaneSendQueue;
    xDataPlaneCtx.xControlPlaneResponseBuff = xControlPlaneResponseBuff;
    xDataPlaneCtx.xDataPlaneSendQueue = xDataPlaneSendQueue;
    xDataPlaneCtx.pxNetif = &( pxCtx->xNetif );

    /* Construct controlplane context */
    xControlPlaneCtx.pxEventCallbackCtx = pxCtx;
    xControlPlaneCtx.xEventCallback = vMxStatusNotify;
    xControlPlaneCtx.xControlPlaneResponseBuff = xControlPlaneResponseBuff;
    xControlPlaneCtx.xDataPlaneTaskHandle = NULL;
    xControlPlaneCtx.xControlPlaneSendQueue = xControlPlaneSendQueue;
    xControlPlaneCtx.pulTxPacketsWaiting = &( xDataPlaneCtx.ulTxPacketsWaiting );
}



/*
 * Networking thread main function.
 */
void net_main( void * pvParameters )
{
    BaseType_t xResult = 0;


    MxNetConnectCtx_t xCtx;
    struct netif * pxNetif = &( xCtx.xNetif );

    /* Set static task handle var for callbacks */
    xNetTaskHandle = xTaskGetCurrentTaskHandle();

    vInitializeContexts( &xCtx );

    /* Initialize lwip */
    tcpip_init( vLwipReadyCallback, &xCtx );

    /* Wait for lwip ready callback */
    xResult = ulWaitForNotifyBits( NET_EVT_IDX,
                                   NET_LWIP_READY_BIT,
                                   portMAX_DELAY );

    /* Start dataplane thread (does hw reset on initialization) */
    xResult = xTaskCreate( &vDataplaneThread,
                           "MxData",
                           4096,
                           &xDataPlaneCtx,
                           25,
                           &xDataPlaneCtx.xDataPlaneTaskHandle );

    configASSERT( xResult == pdTRUE );
    xControlPlaneCtx.xDataPlaneTaskHandle = xDataPlaneCtx.xDataPlaneTaskHandle;
    xCtx.xDataPlaneTaskHandle = xDataPlaneCtx.xDataPlaneTaskHandle;

    /* Start control plane thread */
    xResult = xTaskCreate( &prvControlPlaneRouter,
                           "MxCtrl",
                           4096,
                           &xControlPlaneCtx,
                           24,
                           NULL );

    configASSERT( xResult == pdTRUE );

    /* vInitializeWifiModule returns after receiving a firmware revision and mac address */
    vInitializeWifiModule( &xCtx );


    err_t xLwipError = ERR_OK;

    /* Register lwip netif */
    xLwipError = netifapi_netif_add( pxNetif,
                                     NULL, NULL, NULL,
                                     &xCtx,
                                     &prvInitNetInterface,
                                     tcpip_input );

    configASSERT( xLwipError == ERR_OK );

    netifapi_netif_set_default( pxNetif );

    netifapi_netif_set_up( pxNetif );

    ( void ) xEventGroupSetBits( xSystemEvents, EVT_MASK_NET_INIT );

    /* If already connected to the AP, bring interface up */
    if( xCtx.xStatus >= MX_STATUS_STA_UP )
    {
        vSetAdminUp( pxNetif );
        vStartDhcp( pxNetif );
    }

    /* Outer loop. Reinitializing */
    for( ; ; )
    {
        /* Make a connection attempt */
        if( ( xCtx.xStatus != MX_STATUS_STA_UP ) &&
            ( xCtx.xStatus != MX_STATUS_STA_GOT_IP ) )
        {
            xConnectToAP( &xCtx );
        }

        /*
         * Wait for any event or timeout after 30 seconds
         */
        uint32_t ulNotificationValue = 0x0;
        xResult = xTaskNotifyWaitIndexed( NET_EVT_IDX,
                                          0x0,
                                          0xFFFFFFFF,
                                          &ulNotificationValue,
                                          pdMS_TO_TICKS( 30 * 1000 ) );

        if( ulNotificationValue != 0 )
        {
            /* Latch in current flags */
            uint8_t ucNetifFlags = pxNetif->flags;

            /* Handle state changes from the driver */
            if( ( ulNotificationValue & MX_STATUS_UPDATE_BIT ) )
            {
                vHandleMxStatusUpdate( &xCtx );
            }

            if( ulNotificationValue & NET_LWIP_IP_CHANGE_BIT )
            {
                LogSys( "IP Address Change." );
                vLogAddress( "IP Address:", pxNetif->ip_addr );
                vLogAddress( "Gateway:", pxNetif->gw );
                vLogAddress( "Netmask:", pxNetif->netmask );

                lwiperf_start_tcp_server_default( NULL, NULL );
                LogSys( "Started Iperf server" );

                ( void ) xEventGroupSetBits( xSystemEvents, EVT_MASK_NET_CONNECTED );
            }

            if( ulNotificationValue & NET_LWIP_IFUP_BIT )
            {
                LogInfo( "Administrative UP event." );

                vStartDhcp( pxNetif );
            }
            else if( ( ulNotificationValue & NET_LWIP_LINK_UP_BIT ) &&
                     ( ucNetifFlags & NETIF_FLAG_LINK_UP ) )
            {
                LogInfo( "Link UP event." );

                vSetAdminUp( pxNetif );
                vStartDhcp( pxNetif );
                LogSys( "Network Link Up." );
            }
            else if( ulNotificationValue & NET_LWIP_IFDOWN_BIT )
            {
                LogInfo( "Administrative DOWN event." );

                vStopDhcp( pxNetif );
                vClearAddress( pxNetif );
                ( void ) xEventGroupClearBits( xSystemEvents, EVT_MASK_NET_CONNECTED );
            }
            else if( ( ulNotificationValue & NET_LWIP_LINK_DOWN_BIT ) &&
                     ( ( ucNetifFlags & NETIF_FLAG_LINK_UP ) == 0 ) )
            {
                vSetAdminDown( pxNetif );
                vStopDhcp( pxNetif );
                vClearAddress( pxNetif );
                LogSys( "Network Link Down." );
                ( void ) xEventGroupClearBits( xSystemEvents, EVT_MASK_NET_CONNECTED );
            }

            /* Reconnect requested by configStore or cli process */
            if( ulNotificationValue & ASYNC_REQUEST_RECONNECT_BIT )
            {
                ( void ) xEventGroupClearBits( xSystemEvents, EVT_MASK_NET_CONNECTED );
                ( void ) mx_SetBypassMode( pdFALSE, pdMS_TO_TICKS( 1000 ) );
                ( void ) mx_Disconnect( pdMS_TO_TICKS( 1000 ) );
                xConnectToAP( &xCtx );
            }
        }
    }
}
