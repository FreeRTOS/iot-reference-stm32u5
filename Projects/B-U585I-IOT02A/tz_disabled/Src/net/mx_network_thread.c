/*
 * FreeRTOS STM32 Reference Integration
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
#define LOG_LEVEL LOG_DEBUG
#include "logging.h"

#include <stdint.h>
#include <limits.h>

#include "network_thread.h"
#include "mx_prv.h"

#include "FreeRTOS.h"
#include "task.h"
#include "ConfigStore.h"
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"


#define NET_STATE_UPDATE_BIT 		0x1
#define NET_RECONNECT_REQ_BIT 	    0x2
#define NET_LWIP_READY_BIT          0x4
#define NET_LWIP_IFUP_BIT           0x8
#define NET_LWIP_IFDOWN_BIT         0x10

static TaskHandle_t xNetTaskHandle = NULL;
static MxDataplaneCtx_t xDataPlaneCtx;
static ControlPlaneCtx_t xControlPlaneCtx;

/*
 * @brief Converts from a MxEvent_t to a C string.
 */
static const char* pcMxStatusToString( MxStatus_t xStatus )
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

static BaseType_t waitForNotification( BaseType_t notifyBits, TickType_t xTicksToWait )
{
	uint32_t ulNotifiedValue = 0;
	TickType_t xRemainingTicks = xTicksToWait;
	TimeOut_t xTimeOut;

	vTaskSetTimeOutState( &xTimeOut );

	while( ( ulNotifiedValue & notifyBits ) == 0 )
	{
		/* Note: xTaskCheckForTimeOut adjusts xRemainingTicks */
		if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTicks ) == pdTRUE )
		{
			/* Timed out. Exit loop */
			break;
		}

		(void) xTaskNotifyWait( pdFALSE, notifyBits, &ulNotifiedValue, xRemainingTicks );
	}
	return ( ulNotifiedValue & notifyBits );
}

static BaseType_t waitForNetifState( MxStatus_t xTargetStatus, MxNetConnectCtx_t * pxCtx, TickType_t xTicksToWait )
{
	TickType_t xRemainingTicks = xTicksToWait;
	TimeOut_t xTimeOut;

	if( pxCtx->xStatus == xTargetStatus )
	{
	    return pdTRUE;
	}

	LogDebug( "Starting wait for MX status: %s", pcMxStatusToString( xTargetStatus ) );

	vTaskSetTimeOutState( &xTimeOut );

	while( pxCtx->xStatus != xTargetStatus )
	{
		waitForNotification( NET_STATE_UPDATE_BIT, xRemainingTicks );

		/* Note: xTaskCheckForTimeOut adjusts xRemainingTicks */
		if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTicks ) == pdTRUE )
		{
			LogDebug( "Timed out while waiting for mx status: %s", pcMxStatusToString( xTargetStatus ) );
			/* Timed out. Exit loop */
			break;
		}
	}
	return( pxCtx->xStatus == xTargetStatus );
}

BaseType_t net_request_reconnect( void )
{
    BaseType_t xReturn = pdFALSE;
    if( xNetTaskHandle != NULL )
    {
        xReturn = xTaskNotify( xNetTaskHandle,
                               NET_RECONNECT_REQ_BIT,
                               eSetBits );
    }
    return xReturn;
}

static void vHandleStateTransition( MxNetConnectCtx_t * pxCtx,
                                    MxStatus_t xPreviousStatus,
                                    MxStatus_t xNewStatus )
{
    switch( xNewStatus )
    {
    case MX_STATUS_STA_UP:
        netif_set_link_up( &( pxCtx->xNetif ) );
        break;
    case MX_STATUS_STA_DOWN:
        netif_set_link_down( &( pxCtx->xNetif ) );
        break;
    default:
        break;
    }

    LogDebug( "Mx Status notification: %s -> %s ",
              pcMxStatusToString( xPreviousStatus ),
              pcMxStatusToString( xNewStatus ) );
}


/*
 * Handles network interface state change notifications from the control plane.
 */
static void vMxStatusNotify( MxStatus_t xNewStatus, void * pvCtx )
{
    MxNetConnectCtx_t * pxCtx = ( MxNetConnectCtx_t * ) pvCtx;

	vHandleStateTransition( pxCtx, pxCtx->xStatus, xNewStatus );

	pxCtx->xStatus = xNewStatus;

    (void) xTaskNotify( xNetTaskHandle,
                        NET_STATE_UPDATE_BIT,
                        eSetBits );
}

/* Callback for lwip netif events
 * netif_set_status_callback netif_set_link_callback */
static void vLwipStatusCallback( struct netif *netif )
{
    (void) xTaskNotify( xNetTaskHandle,
                        NET_STATE_UPDATE_BIT,
                        eSetBits );
}

static void vLwipReadyCallback( void * pvCtx )
{
    (void) pvCtx;   /* Unused */

    BaseType_t xReturn = pdFALSE;
    if( xNetTaskHandle != NULL )
    {
        xReturn = xTaskNotify( xNetTaskHandle,
                               NET_LWIP_READY_BIT,
                               eSetBits );
    }
    return xReturn;
}

static BaseType_t xConnectToAP( MxNetConnectCtx_t * pxCtx )
{
    IPCError_t xErr = IPC_SUCCESS;


    if( pxCtx->xStatus == MX_STATUS_NONE ||
        pxCtx->xStatus == MX_STATUS_STA_DOWN )
    {
        xErr |= mx_SetBypassMode( WIFI_BYPASS_MODE_STATION,
                                  pdMS_TO_TICKS( MX_DEFAULT_TIMEOUT_MS ) );

        const char * pcSSID = (char *) ConfigStore_getEntryData( CS_WIFI_PREFERRED_AP_SSID );
        const char * pcPSK = (char *) ConfigStore_getEntryData( CS_WIFI_PREFERRED_AP_CREDENTIALS );

        xErr = mx_Connect( pcSSID, pcPSK, MX_TIMEOUT_CONNECT );

        if( xErr != IPC_SUCCESS)
        {
            LogError("Failed to connect to access point.");
        }
        else
        {
            ( void ) waitForNetifState( MX_STATUS_STA_UP, pxCtx, MX_DEFAULT_TIMEOUT_TICK );
        }
    }

    return( pxCtx->xStatus >= MX_STATUS_STA_UP );
}

//extern const IotMappedPin_t xGpioMap[];

static void vInitDataPlaneCtx( MxDataplaneCtx_t * pxCtx )
{
    extern SPI_HandleTypeDef hspi2;

    pxCtx->pxSpiHandle = &hspi2;
    pxCtx->ulRxPacketsWaiting = 0;
    pxCtx->ulTxPacketsWaiting = 0;

    /* Initialize GPIO pins map / handles */
    //TODO: port to use common io
    pxCtx->gpio_flow = &( xGpioMap[ GPIO_MX_FLOW ] );
    pxCtx->gpio_reset = &( xGpioMap[ GPIO_MX_RESET ] );
    pxCtx->gpio_nss = &( xGpioMap[ GPIO_MX_NSS ] );
    pxCtx->gpio_notify = &( xGpioMap[ GPIO_MX_NOTIFY ] );
}


/*
 * Networking thread main function.
 */
void net_main( void * pvParameters )
{
	BaseType_t xResult = 0;

    MessageBufferHandle_t xControlPlaneResponseBuff, xDataPlaneSendBuff;
    QueueHandle_t xControlPlaneSendQueue;
    MxNetConnectCtx_t xCtx;

    /* Set static task handle var for callbacks */
    xNetTaskHandle = xTaskGetCurrentTaskHandle();

	/* Construct message buffers */
    xDataPlaneSendBuff = xMessageBufferCreate( CONTROL_PLANE_BUFFER_SZ );
    configASSERT( xDataPlaneSendBuff != NULL );

    xControlPlaneResponseBuff = xMessageBufferCreate( CONTROL_PLANE_BUFFER_SZ );
    configASSERT( xControlPlaneResponseBuff != NULL );

    xControlPlaneSendQueue = xQueueCreate( CONTROL_PLANE_QUEUE_LEN, sizeof( PacketBuffer_t * ) );
    configASSERT( xControlPlaneSendQueue != NULL );

    /* Initialize wifi connect context */
    xCtx.xStatus = MX_STATUS_NONE;

    ( void ) memset( &( xCtx.pcFirmwareRevision ), 0, MX_FIRMWARE_REVISION_SIZE );

    ( void ) memset( &( xCtx.xMacAddress ), 0, sizeof( MacAddress_t ) );

    xCtx.xDataPlaneSendBuff = xDataPlaneSendBuff;

	/* Construct dataplane context */
	vInitDataPlaneCtx( &xDataPlaneCtx );
	xDataPlaneCtx.xControlPlaneSendQueue = xControlPlaneSendQueue;
	xDataPlaneCtx.xControlPlaneResponseBuff = xControlPlaneResponseBuff;
	xDataPlaneCtx.xDataPlaneSendBuff = xDataPlaneSendBuff;
	xDataPlaneCtx.pxNetif = &( xCtx.xNetif );

	/* Construct controlplane context */
	xControlPlaneCtx.pxEventCallbackCtx = &xCtx;
	xControlPlaneCtx.xEventCallback = vMxStatusNotify;
	xControlPlaneCtx.xControlPlaneResponseBuff = xControlPlaneResponseBuff;
	xControlPlaneCtx.xDataPlaneTaskHandle = NULL;
	xControlPlaneCtx.xControlPlaneSendQueue = xControlPlaneSendQueue;

    /* Initialize lwip */
    tcpip_init( vLwipReadyCallback, NULL );

    /* Wait for callback */
    xResult = waitForNotification( NET_LWIP_READY_BIT, portMAX_DELAY );

	/* Start dataplane thread (does hw reset on initialization) */
	xResult = xTaskCreate( &vDataplaneThread,
	                       "MxDataPlane",
	                       4096,
	                       &xDataPlaneCtx,
	                       25,
	                       &xDataPlaneCtx.xDataPlaneTaskHandle );

	configASSERT( xResult == pdTRUE );
	xControlPlaneCtx.xDataPlaneTaskHandle = xDataPlaneCtx.xDataPlaneTaskHandle;

	/* Start control plane thread */
	xResult = xTaskCreate( &prvControlPlaneRouter,
	                       "MxControlPlaneRouter",
	                       4096,
	                       &xControlPlaneCtx,
	                       24,
	                       NULL );

	configASSERT( xResult == pdTRUE );

	IPCError_t xErr = IPC_ERROR_INTERNAL;

    while( xErr != IPC_SUCCESS )
    {
        /* Query mac address and firmware revision */
        xErr = mx_RequestVersion( xCtx.pcFirmwareRevision, MX_FIRMWARE_REVISION_SIZE, portMAX_DELAY );

        /* Ensure null termination */
        xCtx.pcFirmwareRevision[ MX_FIRMWARE_REVISION_SIZE ] = '\0';

        /* Get mac address */
        xErr = mx_GetMacAddress( &( xCtx.xMacAddress ), portMAX_DELAY );

        if( xErr != IPC_SUCCESS )
        {
            LogError("Error while querying module firmware revision and mac address.");
        }
        else
        {
            LogInfo( "Firmware Version:   %s", xCtx.pcFirmwareRevision );
            LogInfo( "HW Address:         %02X.%02X.%02X.%02X.%02X.%02X",
                    xCtx.xMacAddress.addr[0], xCtx.xMacAddress.addr[1],
                    xCtx.xMacAddress.addr[2], xCtx.xMacAddress.addr[3],
                    xCtx.xMacAddress.addr[4], xCtx.xMacAddress.addr[5] );
        }
    }

	/* Register lwip netif */
	err_t xLwipError;

	struct netif * pxNetif = netif_add( &( xCtx.xNetif ), NULL, NULL, NULL, &xCtx, &prvInitNetInterface, tcpip_input );

	configASSERT( pxNetif != NULL );

	netif_set_default( &( xCtx.xNetif ) );

	/* Enable lwip callbacks */
	netif_set_status_callback( &( xCtx.xNetif ), vLwipStatusCallback );
	netif_set_link_callback( &( xCtx.xNetif ), vLwipStatusCallback );

	netif_set_up( &( xCtx.xNetif ) );

	/* If already connected to the AP, bring interface up */
	if( xCtx.xStatus >= MX_STATUS_STA_UP )
	{
	    netif_set_link_up( &( xCtx.xNetif ) );
	    xLwipError = dhcp_start( &( xCtx.xNetif ) );
	}

	/* Outer loop. Reinitializing */
	for( ; ; )
	{
	    /* Make a connection attempt */
	    if( xCtx.xStatus < MX_STATUS_STA_UP )
	    {
	        xConnectToAP( &xCtx );
	    }

	    /*
         * Wait for any event or timeout after 5 minutes.
	     * TODO: Backoff timer when not connected
	     * TODO: Constant delay when connected
	     */
	    xResult = waitForNotification( 0xFFFFFFFF, pdMS_TO_TICKS( 30 * 1000 ) );

	    if( xResult != 0 )
	    {
	        /* State update from driver while connected */
	        if( xResult & NET_STATE_UPDATE_BIT )
	        {
	            /* Make a connection attempt */
	            if( xCtx.xStatus < MX_STATUS_STA_UP )
	            {
	                xConnectToAP( &xCtx );
	            }
	            /* Link down, but ip still assigned -> end dhcp */
	            else if( ( xCtx.xNetif.flags & NETIF_FLAG_LINK_UP ) == 0 &&
	                     xCtx.xNetif.ip_addr.addr != 0 )
	            {
	                err_t xLwipError;

	                xLwipError = dhcp_release( &( xCtx.xNetif ) );

	                if( xLwipError != ERR_OK )
	                {
	                    LogError( "lwip dhcp_release returned err code %d.", xLwipError );
	                }
	            }
	            /* Link up without an IP -> start DHCP */
	            else if( ( xCtx.xNetif.flags & NETIF_FLAG_LINK_UP ) != 0 &&
	                     xCtx.xNetif.ip_addr.addr == 0 )
	            {
                    xLwipError = dhcp_start( &( xCtx.xNetif ) );

                    if( xLwipError != ERR_OK )
                    {
                        LogError( "lwip dhcp_start returned err code %d.", xLwipError );
                    }
	            }
	        }

	        /* Reconnect requested by configStore or cli process */
	        if( xResult & NET_RECONNECT_REQ_BIT )
	        {
	            ( void ) mx_Disconnect( pdMS_TO_TICKS( 1000 ) );
	            xConnectToAP( &xCtx );
	        }
	    }
	}

}
