/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

/**
 * @file ota_os_freertos.c
 * @brief Example implementation of the OTA OS Functional Interface for
 * FreeRTOS.
 */

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "queue.h"
#include "timers.h"

/* OTA OS POSIX Interface Includes.*/

#include "ota_demo.h"
#include "ota_os_freertos.h"

/* OTA Event queue attributes.*/
#define MAX_MESSAGES 20
#define MAX_MSG_SIZE sizeof( OtaEventMsg_t )

/* Array containing pointer to the OTA event structures used to send events to
 * the OTA task. */
static OtaEventMsg_t queueData[ MAX_MESSAGES * MAX_MSG_SIZE ];

/* The queue control structure.  .*/
static StaticQueue_t staticQueue;

/* The queue control handle.  .*/
static QueueHandle_t otaEventQueue;

OtaOsStatus_t OtaInitEvent_FreeRTOS()
{
    OtaOsStatus_t otaOsStatus = OtaOsSuccess;

    otaEventQueue = xQueueCreateStatic( ( UBaseType_t ) MAX_MESSAGES,
                                        ( UBaseType_t ) MAX_MSG_SIZE,
                                        ( uint8_t * ) queueData,
                                        &staticQueue );

    if( otaEventQueue == NULL )
    {
        otaOsStatus = OtaOsEventQueueCreateFailed;

        printf( "Failed to create OTA Event Queue: "
                "xQueueCreateStatic returned error: "
                "OtaOsStatus_t=%d \n",
                ( int ) otaOsStatus );
    }
    else
    {
        printf( "OTA Event Queue created.\n" );
    }

    return otaOsStatus;
}

OtaOsStatus_t OtaSendEvent_FreeRTOS( const void * pEventMsg )
{
    OtaOsStatus_t otaOsStatus = OtaOsSuccess;
    BaseType_t retVal = pdFALSE;

    /* Send the event to OTA event queue.*/
    retVal = xQueueSendToBack( otaEventQueue, pEventMsg, ( TickType_t ) 0 );

    if( retVal == pdTRUE )
    {
        printf( "OTA Event Sent.\n" );
    }
    else
    {
        otaOsStatus = OtaOsEventQueueSendFailed;

        printf( "Failed to send event to OTA Event Queue: "
                "xQueueSendToBack returned error: "
                "OtaOsStatus_t=%d \n",
                ( int ) otaOsStatus );
    }

    return otaOsStatus;
}

OtaOsStatus_t OtaReceiveEvent_FreeRTOS( void * pEventMsg )
{
    OtaOsStatus_t otaOsStatus = OtaOsSuccess;
    BaseType_t retVal = pdFALSE;

    retVal = xQueueReceive( otaEventQueue, (OtaEventMsg_t *) pEventMsg, pdMS_TO_TICKS( 1000U ) );

    if( retVal == pdTRUE )
    {
        printf( "OTA Event received \n" );
    }
    else
    {
        otaOsStatus = OtaOsEventQueueReceiveFailed;

        printf( "Failed to receive event or timeout from OTA Event Queue: "
                "xQueueReceive returned error: "
                "OtaOsStatus_t=%d \n",
                ( int ) otaOsStatus );
    }

    return otaOsStatus;
}

void OtaDeinitEvent_FreeRTOS()
{

    vQueueDelete( otaEventQueue );

    printf( "OTA Event Queue Deleted. \n" );

}
