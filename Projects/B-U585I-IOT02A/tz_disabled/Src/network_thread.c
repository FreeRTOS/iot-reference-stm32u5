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

#include "FreeRTOS.h"
#include "task.h"

#include "net_connect.h"
#include "ConfigStore.h"



#define NET_STATE_UPDATE_BIT 		0x1
#define NET_RECONNECT_REQ_BIT 	    0x2

#define DEFAULT_TIMEOUT

extern int32_t mx_wifi_driver(net_if_handle_t *pnetif);

static net_state_t previousNetState = NET_STATE_DEINITIALIZED;

static TaskHandle_t netTaskHandle = NULL;

/*
 * @brief Converts from a net_state_t to a C string.
 */
static const char* ns_to_str( net_state_t st )
{
	const char * ret = "Unknown";
	switch( st )
	{
	case NET_STATE_DEINITIALIZED:
		ret = "Not Initialized";
		break;
	case NET_STATE_INITIALIZED:
		ret = "Initialized";
		break;
	case  NET_STATE_STARTING:
		ret = "Starting";
		break;
	case  NET_STATE_READY:
		ret = "Ready";
		break;
	case  NET_STATE_CONNECTING:
		ret = "Connecting";
		break;
	case  NET_STATE_CONNECTED:
		ret = "Connected";
		break;
	case  NET_STATE_STOPPING:
		ret = "Stopping";
		break;
	case  NET_STATE_DISCONNECTING:
		ret = "Disconnecting";
		break;
	case  NET_STATE_CONNECTION_LOST:
		ret = "Connection Lost";
		break;
	default:
		/* default to "Unknown" string */
		break;
	}
	return ret;
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

static BaseType_t waitForNetifState( net_state_t xTargetState, net_if_handle_t * pxNetIf, TickType_t xTicksToWait )
{
	TickType_t xRemainingTicks = xTicksToWait;
	TimeOut_t xTimeOut;

	if( pxNetIf->state == xTargetState )
	{
	    return pdTRUE;
	}

	LogDebug( "Starting wait for xNetIf state: \"%s\"", ns_to_str( xTargetState ) );

	vTaskSetTimeOutState( &xTimeOut );

	while( pxNetIf->state != xTargetState )
	{
		waitForNotification( NET_STATE_UPDATE_BIT, xRemainingTicks );

		/* Note: xTaskCheckForTimeOut adjusts xRemainingTicks */
		if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTicks ) == pdTRUE )
		{
			LogDebug( "Timed out while waiting for xNetIf state: %s", ns_to_str( xTargetState ) );
			/* Timed out. Exit loop */
			break;
		}
	}
	return( pxNetIf->state == xTargetState );
}

BaseType_t net_request_reconnect( void )
{
    return xTaskNotify( netTaskHandle,
                        NET_RECONNECT_REQ_BIT,
                        eSetBits );
}


/*
 * Handles network interface state notifications from the STM32 Networking Library
 */
static void netStateNotify( net_state_t newState )
{
	LogDebug( "netif stat notification: %s -> %s ",
			  ns_to_str( previousNetState ),
			  ns_to_str( newState ) );
	configASSERT( netTaskHandle );

	// TODO: Determine if callback is called from ISR or not.
	if( xPortIsInsideInterrupt() )
	{
		(void) xTaskNotifyFromISR( netTaskHandle,
							       NET_STATE_UPDATE_BIT,
							       eSetBits,
							       NULL );
	}
	else
	{
		(void) xTaskNotify( netTaskHandle,
                            NET_STATE_UPDATE_BIT,
                            eSetBits );
	}


//	switch( newState )
//	{
//	/* Ignore initialized and Starting states */
//	case NET_STATE_INITIALIZED:
//	case NET_STATE_STARTING:
//		break;
//	case NET_STATE_READY:
//	{
////		LogInfo( ( "Device Name: 		%s", netif->DeviceName ) );
////		LogInfo( ( "Device ID:	 		%s", netif->DeviceID ) );
////		LogInfo( ( "Firmware Version: 	%s", netif->DeviceID ) );
////		LogInfo( ( "HW Address:			%02X.%02X.%02X.%02X.%02X.%02X",
////				netif->macaddr.mac[0], netif->macaddr.mac[1],
////				netif->macaddr.mac[2], netif->macaddr.mac[3],
////				netif->macaddr.mac[4], netif->macaddr.mac[5] ));
////		// Trigger task notification
//	}
//	case NET_STATE_CONNECTING:
//	case NET_STATE_CONNECTED:
//		// Trigger task notification
//	case NET_STATE_STOPPING:
//	case NET_STATE_DISCONNECTING:
//	case NET_STATE_CONNECTION_LOST:
//		break;
//	default:
//		break;
//	}

	previousNetState = newState;

}

/* Handler for STM32 Network library notifications */
static void netNotifyCallback( void *ctx, uint32_t eventClass, uint32_t eventID, void *eventData )
{
	(void) eventData;

	switch( eventClass )
	{
	case NET_EVENT_STATE_CHANGE:
		netStateNotify( ( net_state_t ) eventID );
		break;
	case NET_EVENT:
	case NET_EVENT_WIFI:
	default:
		LogWarn( "Unhandled network event class: %u", eventClass );
		break;
	}
}

//static void wifi_scan( void )
//{
//	/* Call mx_wifi_scan via netif interface (blocking call) */
//	netRslt = net_wifi_scan(netif, NET_WIFI_SCAN_PASSIVE, NULL); //TODO: consider NET_WIFI_SCAN_AUTO or NET_WIFI_SCAN_ACTIVE
//
//	if( netRslt != NET_OK )
//	{
//		LogError( ( "Failed to scan for WiFi Access Points" ) );
//	}
//	else
//	{
//		#define MAX_AP_SCAN_LEN 25
//		/* Call mx_wifi_get_scan_result via netif */
//		/*
//		 *  Length of mxwifi block type is 48 bytes, mx_wifi_get_scan_result
//		 * temporarily allocates 48 x scan_bss_array_size and copies values
//		 * to the user provided buffer.
//		 *
//		 */
//		net_wifi_scan_results_t * pScanRslt = pvPortMalloc( MAX_AP_SCAN_LEN * sizeof( net_wifi_scan_results_t ) );
//		netRslt = net_wifi_get_scan_results( netif, pScanRslt, MAX_AP_SCAN_LEN );
//
//		if( netRslt > 0 )
//		{
//			int8_t bestRssi = INT8_MIN;
//			uint32_t bestRssiIdx = UINT32_MAX;
//
//			char * prefSSID = ( char * ) ConfigStore_getEntryData( CS_WIFI_PREFERRED_AP_SSID );
//			size_t prefSSIDLen = ConfigStore_getEntrySize( CS_WIFI_PREFERRED_AP_SSID );
//
//			for( uint32_t i = 0; i < netRslt; i++ )
//			{
//				if( strncmp( prefSSID, pScanRslt[ i ].ssid.value, prefSSIDLen ) == 0 )
//				{
//					if( bestRssi < pScanRslt[ i ].rssi)
//					{
//						bestRssi = pScanRslt[ i ].rssi;
//						bestRssiIdx = i;
//					}
//				}
//			}
//		}
//
//	}
//}


static int32_t netConnectToAP( net_if_handle_t * pxNetIf )
{
	int32_t netRslt = NET_ERROR_GENERIC;

	net_wifi_credentials_t xWifiCredentials;

	xWifiCredentials.ssid = (char *) ConfigStore_getEntryData( CS_WIFI_PREFERRED_AP_SSID );
	xWifiCredentials.psk = (char *) ConfigStore_getEntryData( CS_WIFI_PREFERRED_AP_CREDENTIALS );
	xWifiCredentials.security_mode = NET_WIFI_SM_AUTO;

	/* get connection / auth data */
	(void) net_wifi_set_credentials( pxNetIf, &xWifiCredentials );
	(void) net_wifi_set_access_mode( pxNetIf, NET_WIFI_MODE_STA );

	/* Enable DHCP */
	netRslt = net_if_set_dhcp_mode( pxNetIf, true );

	configASSERT( netRslt == NET_OK );

	/* Search for and connect to AP */
	netRslt = net_if_start( pxNetIf );

	if( netRslt != NET_OK )
	{
		LogError( "Error while starting network interface: %d", netRslt );
	}

	/* Clear WiFi connection data from ram. */
	memset( &xWifiCredentials, 0, sizeof( xWifiCredentials ) );

	return netRslt;
}


//static void netCleanup( net_if_handle_t * xNetIf )
//{
//	int32_t netRslt = NET_ERROR_GENERIC;
//
//	netRslt = net_if_stop( xNetIf );
//
//	if( netRslt != NET_OK )
//	{
//		LogError( "Error while stopping network interface." );
//	}
//
//	(void) waitForNetifState( NET_STATE_INITIALIZED, xNetIf, pdMS_TO_TICKS( 1000 ) );
//
//	netRslt = net_if_deinit( xNetIf );
//
//	if( netRslt != NET_OK )
//	{
//		LogError( "Error while deinitializing network interface." );
//	}
//
//	//TODO: Clear secrets from module if stored??
//}

static BaseType_t doConnectSequence( net_if_handle_t * pxNetIf, net_event_handler_t * pxNetHandler )
{
    if( pxNetIf->state == NET_STATE_DEINITIALIZED )
    {
        if( net_if_init( pxNetIf, mx_wifi_driver, pxNetHandler ) != NET_OK )
        {
            return pdFALSE;
        }

        if( !waitForNetifState( NET_STATE_INITIALIZED, pxNetIf, pdMS_TO_TICKS( 1000 ) ) )
        {
            return pdFALSE;
        }
    }

    if( pxNetIf->state == NET_STATE_INITIALIZED )
    {
        LogInfo( "Device Name:        %s", pxNetIf->DeviceName );
        LogInfo( "Device ID:          %s", pxNetIf->DeviceID );
        LogInfo( "Firmware Version:   %s", pxNetIf->DeviceID );
        LogInfo( "HW Address:         %02X.%02X.%02X.%02X.%02X.%02X",
                pxNetIf->macaddr.mac[0], pxNetIf->macaddr.mac[1],
                pxNetIf->macaddr.mac[2], pxNetIf->macaddr.mac[3],
                pxNetIf->macaddr.mac[4], pxNetIf->macaddr.mac[5] );

        if( netConnectToAP( pxNetIf ) != NET_OK )
        {
            return pdFALSE;
        }

        /* Wait up to 60 seconds to connect to AP */
        if( !waitForNetifState( NET_STATE_READY, pxNetIf, pdMS_TO_TICKS( 60 * 1000 ) ) )
        {
            LogInfo("Timeout while attempting to connect to access point.");
            return pdFALSE;
        }
    }

    if( pxNetIf->state == NET_STATE_READY )
    {
        if( net_if_connect( pxNetIf ) != NET_OK )
        {
            LogError( "An error occurred when requesting an IP address." );
            return pdFALSE;
        }

        if( !waitForNetifState( NET_STATE_CONNECTED, pxNetIf, pdMS_TO_TICKS( 30 * 1000 ) ) )
        {
            LogWarn( "Timed out while waiting for an IP address." );
            return pdFALSE;
        }
    }
    return( pxNetIf->state == NET_STATE_CONNECTED );
}

static void doResetSequence( net_if_handle_t * pxNetIf, net_event_handler_t * pxNetHandler )
{
    if( pxNetIf-> state > NET_STATE_INITIALIZED )
    {
        (void) net_if_stop( pxNetIf );
        waitForNetifState( NET_STATE_INITIALIZED, pxNetIf, pdMS_TO_TICKS( 10 * 1000 ) );
    }

    if( pxNetIf-> state > NET_STATE_DEINITIALIZED )
    {
        (void) net_if_deinit( pxNetIf );
        waitForNetifState( NET_STATE_DEINITIALIZED, pxNetIf, pdMS_TO_TICKS( 10* 1000 ) );
    }

    memset( pxNetIf, 0, sizeof( net_if_handle_t ) );
    memset( pxNetHandler, 0, sizeof( net_event_handler_t ) );

    pxNetHandler->callback = netNotifyCallback;
    pxNetHandler->context = pxNetIf;

    (void) doConnectSequence( pxNetIf, pxNetHandler );

}

static void doDisconnectSequence( net_if_handle_t * pxNetIf )
{
    (void) net_if_stop( pxNetIf );
    waitForNetifState( NET_STATE_INITIALIZED, pxNetIf, pdMS_TO_TICKS( 10 * 1000 ) );
}

/*
 * Networking thread main function.
 */
void net_main( void * pvParameters )
{
	BaseType_t xNotification = 0;

	static net_if_handle_t xNetIf;
	static net_event_handler_t xNetHandler;

	memset( &xNetIf, 0, sizeof( net_if_handle_t ) );
	memset( &xNetHandler, 0, sizeof( net_event_handler_t ) );

	xNetHandler.callback = netNotifyCallback;
	xNetHandler.context = &xNetIf;

	netTaskHandle = xTaskGetCurrentTaskHandle();



	LogInfo("Thread Starting");

	/* Outer loop. Reinitializing */
	for( ; ; )
	{
	    LogInfo("Thread loop");
	    /* Make a connection attempt */
	    if( xNetIf.state != NET_STATE_CONNECTED )
	    {
	        doConnectSequence( &xNetIf, &xNetHandler );
	    }

	    /*
         * Wait for any event or timeout after 5 minutes.
	     * TODO: Backoff timer when not connected
	     * TODO: Constant delay when connected
	     */
	    xNotification = waitForNotification( 0xFFFFFFFF, pdMS_TO_TICKS( 300 * 1000 ) );


	    if( xNotification != 0 )
	    {
	        /* State update from driver while connected */
	        if( xNotification & NET_STATE_UPDATE_BIT )
	        {
	            if( xNetIf.state < NET_STATE_CONNECTED )
	            {
	                doConnectSequence( &xNetIf, &xNetHandler );
	            }
	            else if( xNetIf.state == NET_STATE_CONNECTION_LOST )
	            {
	                /* Wait 60 seconds before attempting to reconnect */
	                vTaskDelay( pdMS_TO_TICKS( 60 * 1000 ) );
	                if( xNetIf.state != NET_STATE_CONNECTED )
	                {
	                    doConnectSequence( &xNetIf, &xNetHandler );
	                }
	            }
	        }

	        /* Reconnect requested by configStore or cli process */
	        if( xNotification & NET_RECONNECT_REQ_BIT )
	        {
	            doDisconnectSequence( &xNetIf );
	            doConnectSequence( &xNetIf, &xNetHandler );
	        }
	    }
	    else // Timeout case
	    {
	        // TODO: periodic health check if connected.
	        // TODO: Periodic hard reset if disconnected.
	    }


	    /* timeout
	    if( xNotification == 0 )
	    {

	    }




//	    /* Received a notification from the driver of a state transition */
//		if( xNotification & ( NET_STATE_UPDATE_BIT ) )
//		{
//		    switch( xNetIf.state )
//		    {
//		        case NET_STATE_DEINITIALIZED:
//		        {
//		            netRslt = net_if_init( &xNetIf, mx_wifi_driver, &xNetHandler );
//		            // TODO: Schedule retry timer
//		            break;
//		        }
//		        case NET_STATE_INITIALIZED:
//		        {
//	                LogInfo( ( "Device Name:        %s", netif->DeviceName ) );
//	                LogInfo( ( "Device ID:          %s", netif->DeviceID ) );
//	                LogInfo( ( "Firmware Version:   %s", netif->DeviceID ) );
//	                LogInfo( ( "HW Address:         %02X.%02X.%02X.%02X.%02X.%02X",
//	                        netif->macaddr.mac[0], netif->macaddr.mac[1],
//	                        netif->macaddr.mac[2], netif->macaddr.mac[3],
//	                        netif->macaddr.mac[4], netif->macaddr.mac[5] ));
//
//	                netConnectToAP( &xNetIf );
//	                break;
//		        }
//		        case NET_STATE_STARTING:
//		            break;
//		        case NET_STATE_READY:
//		        {
//	                // TODO: Cancel retry timer from < NET_STATE_READY
//	                netRslt = net_if_connect( &xNetIf );
//	                if( netRslt != NET_OK )
//	                {
//	                    LogError( "Error when initializing ip stack with net_if_connect. " );
//	                }
//	                // TODO: Increment backoff, Schedule retry timer
//		        }
//		        case NET_STATE_CONNECTING:
//		            break;
//		        case NET_STATE_CONNECTED:
//		            /* TODO: Cancel retry timers. Clear backoff timer state. */
//		                            /* TODO: Schedule periodic health check */
//		            break;
//		        case NET_STATE_STOPPING:
//		            break;
//		        case NET_STATE_DISCONNECTING:
//		            break;
//		        case NET_STATE_CONNECTION_LOST:
//		            break;
//		        default:
//		            break;
//
//		    }
//		}
//
//		/* Received a request to connect or reconnect / timer expired */
//		if( xNotification & NET_CONNECT_ATTEMPT_BIT )
//		{
//		    switch( xNetIf.state )
//		    {
//		        case NET_STATE_DEINITIALIZED:
//		            netRslt = net_if_init( &xNetIf, mx_wifi_driver, &xNetHandler );
//		            break;
//		        case NET_STATE_INITIALIZED:
//		            netConnectToAP( &xNetIf );
//		            break;
//		        case NET_STATE_STARTING:
//		        case NET_STATE_READY:
//                    // TODO: Cancel retry timer from < NET_STATE_READY
//                    netRslt = net_if_connect( &xNetIf );
//                    if( netRslt != NET_OK )
//                    {
//                        LogError( "Error when initializing ip stack with net_if_connect. " );
//                    }
//                    break;
//		        case NET_STATE_CONNECTING:
//		        case NET_STATE_CONNECTED:
//		        case NET_STATE_STOPPING:
//		        case NET_STATE_DISCONNECTING:
//		        case NET_STATE_CONNECTION_LOST:
//		    }
//			/* Attempt to connect */
//			if( xNetIf.state < NET_STATE_READY )
//			{
//				netConnectToAP( &xNetIf );
//				/* Schedule retry timer */
//			}
//			/* Connected to AP, need to fetch ip address from module and setup ip stack */
//			else if ( xNetIf.state == NET_STATE_READY )
//			{
//				netRslt = net_if_connect( &xNetIf );
//				if( netRslt != NET_OK )
//				{
//					LogError( "Error when initializing ip stack with net_if_connect." );
//				}
//				/* Schedule retry timer */
//
//			}
//			else if( xNetIf.state == NET_STATE_CONNECTION_LOST )
//			{
//				/* Schedule reconnection timer with backoff */
//
//			}
//		}
//
//		/* Wait for any event */
//		xNotification = waitForNotification( 0xFFFFFFFF, portMAX_DELAY );
	}

}
