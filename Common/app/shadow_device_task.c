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

/*
 * Demo for showing how to use the Device Shadow library's API. This version
 * of the Device Shadow API provides macros and helper functions for assembling MQTT topics
 * strings, and for determining whether an incoming MQTT message is related to the
 * device shadow.
 *
 * This example assumes there is a powerOn state in the device shadow. It does the
 * following operations:
 * 1. Assemble strings for the MQTT topics of device shadow, by using macros defined by the Device Shadow library.
 * 2. Subscribe to those MQTT topics using the MQTT Agent.
 * 3. Register callbacks for incoming shadow topic publishes with the subsciption_manager.
 * 3. Publish to report the current state of powerOn.
 * 5. Check if powerOn has been changed and send an update if so.
 * 6. If a publish to update reported state was sent, wait until either prvIncomingPublishUpdateAcceptedCallback
 *    or prvIncomingPublishUpdateRejectedCallback handle the response.
 * 7. Wait until time for next check and repeat from step 5.
 *
 * Meanwhile, when prvIncomingPublishUpdateDeltaCallback receives changes to the shadow state,
 * it will apply them on the device.
 */

#include "logging_levels.h"
/* define LOG_LEVEL here if you want to modify the logging level from the default */

#define LOG_LEVEL    LOG_INFO

#include "logging.h"

/* Standard includes. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "sys_evt.h"

/* MQTT library includes. */
#include "core_mqtt_agent.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

/* JSON library includes. */
#include "core_json.h"

/* Shadow API header. */
#include "shadow.h"

#include "kvstore.h"

#include "hw_defs.h"

/**
 * @brief Format string representing a Shadow document with a "reported" state.
 *
 * The real json document will look like this:
 * {
 *   "state": {
 *     "reported": {
 *       "powerOn": 1
 *     }
 *   },
 *   "clientToken": "021909"
 * }
 *
 * Note the client token, which is optional. The token is used to identify the
 * response to an update. The client token must be unique at any given time,
 * but may be reused once the update is completed. For this demo, a timestamp
 * is used for a client token.
 */
#define shadowexampleSHADOW_REPORTED_JSON \
    "{"                                   \
    "\"state\":{"                         \
    "\"reported\":{"                      \
    "\"powerOn\":%1u"                     \
    "}"                                   \
    "},"                                  \
    "\"clientToken\":\"%06lu\""           \
    "}"

/**
 * @brief The expected size of #SHADOW_REPORTED_JSON.
 *
 * Since all of the format specifiers in #SHADOW_REPORTED_JSON include a length,
 * its actual size can be calculated at compile time from the difference between
 * the lengths of the format strings and their formatted output. We must subtract 2
 * from the length as according the following formula:
 * 1. The length of the format string "%1u" is 3.
 * 2. The length of the format string "%06lu" is 5.
 * 3. The formatted length in case 1. is 1 ( for the state of powerOn ).
 * 4. The formatted length in case 2. is 6 ( for the clientToken length ).
 * 5. Thus the additional size of our format is 2 = 3 + 5 - 1 - 6 + 1 (termination character).
 *
 * Custom applications may calculate the length of the JSON document with the same method.
 */
#define shadowexampleSHADOW_REPORTED_JSON_LENGTH       ( sizeof( shadowexampleSHADOW_REPORTED_JSON ) - 2 )

/**
 * @brief Time in ms to wait between checking for updates to report.
 */
#define shadowMS_BETWEEN_REPORTS                       ( 15000U )

/**
 * @brief This demo uses task notifications to signal tasks from MQTT callback
 * functions. shadowexampleMS_TO_WAIT_FOR_NOTIFICATION defines the time, in ticks,
 * to wait for such a callback.
 */
#define shadow_SIGNAL_TIMEOUT                          ( 30 * 1000 )

/**
 * @brief The maximum amount of time in milliseconds to wait for the commands
 * to be posted to the MQTT agent should the MQTT agent's command queue be full.
 * Tasks wait in the Blocked state, so don't use any CPU time.
 */
#define shadowexampleMAX_COMMAND_SEND_BLOCK_TIME_MS    ( 60 * 1000 )

/**
 * @brief An invalid value for the powerOn state. This is used to set the last
 * reported state to a value that will not match the current state. As we only
 * set the powerOn state to 0 or 1, any other value will suffice.
 */
#define shadowexampleINVALID_POWERON_STATE             ( 2 )

/**
 * @brief Defines structure passed to callbacks and local functions.
 */
typedef struct MQTTAgentCommandContext
{
    char * pcDeviceName;
    uint8_t ucDeviceNameLen;
    char * pcTopicUpdate;
    uint16_t usTopicUpdateLen;
    char * pcTopicUpdateDelta;
    uint16_t usTopicUpdateDeltaLen;
    char * pcTopicUpdateAccepted;
    uint16_t usTopicUpdateAcceptedLen;
    char * pcTopicUpdateRejected;
    uint16_t usTopicUpdateRejectedLen;
    char * pcTopicDelete;
    uint16_t usTopicDeleteLen;

    /**
     * @brief The simulated device current power on state.
     */
    uint32_t ulCurrentPowerOnState;

    /**
     * @brief The last reported state. It is initialized to an invalid value so that
     * an update is initially sent.
     */
    uint32_t ulReportedPowerOnState;

    /**
     * @brief Match the received clientToken with the one sent in a device shadow
     * update. Set to 0 when not waiting on a response.
     */
    uint32_t ulClientToken;

    /**
     * @brief The handle of this task. It is used by callbacks to notify this task.
     */
    TaskHandle_t xShadowDeviceTaskHandle;

    MQTTAgentHandle_t xAgentHandle;
} ShadowDeviceCtx_t;

extern MQTTAgentContext_t xGlobalMqttAgentContext;

/*-----------------------------------------------------------*/

/**
 * @brief Subscribe to the used device shadow topics.
 *
 * @return true if the subscribe is successful;
 * false otherwise.
 */
static bool prvSubscribeToShadowUpdateTopics( ShadowDeviceCtx_t * pxCtx );

/**
 * @brief The callback to execute when there is an incoming publish on the
 * topic for delta updates. It verifies the document and sets the
 * powerOn state accordingly.
 */
static void prvIncomingPublishUpdateDeltaCallback( void * pvCtx,
                                                   MQTTPublishInfo_t * pxPublishInfo );

/**
 * @brief The callback to execute when there is an incoming publish on the
 * topic for accepted requests. It verifies the document is valid and is being waited on.
 * If so it updates the last reported state and notifies the task to inform completion
 * of the update request.
 */
static void prvIncomingPublishUpdateAcceptedCallback( void * pvCtx,
                                                      MQTTPublishInfo_t * pxPublishInfo );


/**
 * @brief The callback to execute when there is an incoming publish on the
 * topic for rejected requests. It verifies the document is valid and is being waited on.
 * If so it notifies the task to inform completion of the update request.
 */
static void prvIncomingPublishUpdateRejectedCallback( void * pvCtx,
                                                      MQTTPublishInfo_t * pxPublishInfo );

/**
 * @brief Entry point of shadow demo.
 *
 * This main function demonstrates how to use the macros provided by the
 * Device Shadow library to assemble strings for the MQTT topics defined
 * by AWS IoT Device Shadow. It uses these macros for topics to subscribe
 * to:
 * - SHADOW_TOPIC_STRING_UPDATE_DELTA for "$aws/things/thingName/shadow/update/delta"
 * - SHADOW_TOPIC_STRING_UPDATE_ACCEPTED for "$aws/things/thingName/shadow/update/accepted"
 * - SHADOW_TOPIC_STRING_UPDATE_REJECTED for "$aws/things/thingName/shadow/update/rejected"
 *
 * It also uses these macros for topics to publish to:
 * - SHADOW_TOPIC_STIRNG_DELETE for "$aws/things/thingName/shadow/delete"
 * - SHADOW_TOPIC_STRING_UPDATE for "$aws/things/thingName/shadow/update"
 */
void vShadowDeviceTask( void * pvParameters );

static bool prvInitializeCtx( ShadowDeviceCtx_t * pxCtx )
{
    bool xSuccess = true;

    configASSERT( pxCtx );

    pxCtx->pcDeviceName = NULL;

    /* Note: KVStore_getSize always returns the buffer length needed */
    if( KVStore_getSize( CS_CORE_THING_NAME ) <= UINT8_MAX )
    {
        size_t uxDeviceNameLen = 0;
        pxCtx->pcDeviceName = KVStore_getStringHeap( CS_CORE_THING_NAME, NULL );

        if( pxCtx->pcDeviceName )
        {
            uxDeviceNameLen = strnlen( pxCtx->pcDeviceName, KVStore_getSize( CS_CORE_THING_NAME ) );
        }

        if( uxDeviceNameLen < UINT8_MAX )
        {
            pxCtx->ucDeviceNameLen = ( uint8_t ) uxDeviceNameLen;
        }
    }

    if( ( pxCtx->pcDeviceName != NULL ) &&
        ( pxCtx->ucDeviceNameLen > 0 ) )
    {
        ShadowStatus_t xStatus = SHADOW_SUCCESS;

        pxCtx->usTopicUpdateLen = SHADOW_TOPIC_LENGTH_UPDATE( pxCtx->ucDeviceNameLen );
        pxCtx->pcTopicUpdate = pvPortMalloc( pxCtx->usTopicUpdateLen );

        xStatus |= Shadow_GetTopicString( ShadowTopicStringTypeUpdate,
                                          pxCtx->pcDeviceName,
                                          pxCtx->ucDeviceNameLen,
                                          pxCtx->pcTopicUpdate,
                                          pxCtx->usTopicUpdateLen,
                                          &( pxCtx->usTopicUpdateLen ) );

        pxCtx->usTopicUpdateDeltaLen = SHADOW_TOPIC_LENGTH_UPDATE_DELTA( pxCtx->ucDeviceNameLen );
        pxCtx->pcTopicUpdateDelta = pvPortMalloc( pxCtx->usTopicUpdateDeltaLen );

        xStatus |= Shadow_GetTopicString( ShadowTopicStringTypeUpdateDelta,
                                          pxCtx->pcDeviceName,
                                          pxCtx->ucDeviceNameLen,
                                          pxCtx->pcTopicUpdateDelta,
                                          pxCtx->usTopicUpdateDeltaLen,
                                          &( pxCtx->usTopicUpdateDeltaLen ) );

        pxCtx->usTopicUpdateAcceptedLen = SHADOW_TOPIC_LENGTH_UPDATE_ACCEPTED( pxCtx->ucDeviceNameLen );
        pxCtx->pcTopicUpdateAccepted = pvPortMalloc( pxCtx->usTopicUpdateAcceptedLen );

        xStatus |= Shadow_GetTopicString( ShadowTopicStringTypeUpdateAccepted,
                                          pxCtx->pcDeviceName,
                                          pxCtx->ucDeviceNameLen,
                                          pxCtx->pcTopicUpdateAccepted,
                                          pxCtx->usTopicUpdateAcceptedLen,
                                          &( pxCtx->usTopicUpdateAcceptedLen ) );

        pxCtx->usTopicUpdateRejectedLen = SHADOW_TOPIC_LENGTH_UPDATE_REJECTED( pxCtx->ucDeviceNameLen );
        pxCtx->pcTopicUpdateRejected = pvPortMalloc( pxCtx->usTopicUpdateRejectedLen );

        xStatus |= Shadow_GetTopicString( ShadowTopicStringTypeUpdateRejected,
                                          pxCtx->pcDeviceName,
                                          pxCtx->ucDeviceNameLen,
                                          pxCtx->pcTopicUpdateRejected,
                                          pxCtx->usTopicUpdateRejectedLen,
                                          &( pxCtx->usTopicUpdateRejectedLen ) );


        pxCtx->usTopicDeleteLen = SHADOW_TOPIC_LENGTH_DELETE( pxCtx->ucDeviceNameLen );
        pxCtx->pcTopicDelete = pvPortMalloc( pxCtx->usTopicDeleteLen );

        xStatus |= Shadow_GetTopicString( ShadowTopicStringTypeDelete,
                                          pxCtx->pcDeviceName,
                                          pxCtx->ucDeviceNameLen,
                                          pxCtx->pcTopicDelete,
                                          pxCtx->usTopicDeleteLen,
                                          &( pxCtx->usTopicDeleteLen ) );

        xSuccess &= ( xStatus == SHADOW_SUCCESS );
    }
    else
    {
        xSuccess = false;
    }

    return xSuccess;
}

/*-----------------------------------------------------------*/

static bool prvSubscribeToShadowUpdateTopics( ShadowDeviceCtx_t * pxCtx )
{
    MQTTStatus_t xStatus = MQTTSuccess;

    xStatus = MqttAgent_SubscribeSync( pxCtx->xAgentHandle,
                                       pxCtx->pcTopicUpdateDelta,
                                       MQTTQoS1,
                                       prvIncomingPublishUpdateDeltaCallback,
                                       pxCtx );

    if( xStatus != MQTTSuccess )
    {
        LogError( "Failed to subscribe to topic: %s", pxCtx->pcTopicUpdateDelta );
    }
    else
    {
        xStatus = MqttAgent_SubscribeSync( pxCtx->xAgentHandle,
                                           pxCtx->pcTopicUpdateAccepted,
                                           MQTTQoS1,
                                           prvIncomingPublishUpdateAcceptedCallback,
                                           pxCtx );

        if( xStatus != MQTTSuccess )
        {
            LogError( "Failed to subscribe to topic: %s", pxCtx->pcTopicUpdateAccepted );
        }
    }

    if( xStatus == MQTTSuccess )
    {
        xStatus = MqttAgent_SubscribeSync( pxCtx->xAgentHandle,
                                           pxCtx->pcTopicUpdateRejected,
                                           MQTTQoS1,
                                           prvIncomingPublishUpdateRejectedCallback,
                                           pxCtx );

        if( xStatus != MQTTSuccess )
        {
            LogError( "Failed to subscribe to topic: %s", pxCtx->pcTopicUpdateRejected );
        }
    }

    return( xStatus == MQTTSuccess );
}

/*-----------------------------------------------------------*/

static void prvIncomingPublishUpdateDeltaCallback( void * pvCtx,
                                                   MQTTPublishInfo_t * pxPublishInfo )
{
    static uint32_t ulCurrentVersion = 0; /* Remember the latest version number we've received */
    uint32_t ulVersion = 0UL;
    uint32_t ulNewState = 0UL;
    char * pcOutValue = NULL;
    uint32_t ulOutValueLength = 0UL;
    JSONStatus_t result = JSONSuccess;

    ShadowDeviceCtx_t * pxCtx = ( ShadowDeviceCtx_t * ) pvCtx;

    configASSERT( pxPublishInfo != NULL );
    configASSERT( pxPublishInfo->pPayload != NULL );

    LogDebug( "/update/delta json payload:%.*s.",
              pxPublishInfo->payloadLength,
              ( const char * ) pxPublishInfo->pPayload );

    /* The payload will look similar to this:
     * {
     *      "state": {
     *          "powerOn": 1
     *      },
     *      "metadata": {
     *          "powerOn": {
     *              "timestamp": 1595437367
     *          }
     *      },
     *      "timestamp": 1595437367,
     *      "clientToken": "388062",
     *      "version": 12
     *  }
     */

    /* Make sure the payload is a valid json document. */
    result = JSON_Validate( pxPublishInfo->pPayload,
                            pxPublishInfo->payloadLength );

    if( result != JSONSuccess )
    {
        LogError( "Invalid JSON document received!" );
    }
    else
    {
        /* Obtain the version value. */
        result = JSON_Search( ( char * ) pxPublishInfo->pPayload,
                              pxPublishInfo->payloadLength,
                              "version",
                              sizeof( "version" ) - 1,
                              &pcOutValue,
                              ( size_t * ) &ulOutValueLength );

        if( result != JSONSuccess )
        {
            LogError( "Version field not found in JSON document!" );
        }
        else
        {
            /* Convert the extracted value to an unsigned integer value. */
            ulVersion = ( uint32_t ) strtoul( pcOutValue, NULL, 10 );

            /* Make sure the version is newer than the last one we received. */
            if( ulVersion <= ulCurrentVersion )
            {
                /* In this demo, we discard the incoming message
                 * if the version number is not newer than the latest
                 * that we've received before. Your application may use a
                 * different approach.
                 */
                LogWarn( ( "Received unexpected delta update with version %u. Current version is %u",
                           ( unsigned int ) ulVersion,
                           ( unsigned int ) ulCurrentVersion ) );
            }
            else
            {
                LogInfo( "Received delta update with version %.*s.",
                         ulOutValueLength,
                         pcOutValue );

                /* Set received version as the current version. */
                ulCurrentVersion = ulVersion;

                /* Get powerOn state from json documents. */
                result = JSON_Search( ( char * ) pxPublishInfo->pPayload,
                                      pxPublishInfo->payloadLength,
                                      "state.powerOn",
                                      sizeof( "state.powerOn" ) - 1,
                                      &pcOutValue,
                                      ( size_t * ) &ulOutValueLength );

                if( result != JSONSuccess )
                {
                    LogWarn( "powerOn field not found in JSON document!" );
                }
                else
                {
                    /* Convert the powerOn state value to an unsigned integer value. */
                    ulNewState = ( uint32_t ) strtoul( pcOutValue, NULL, 10 );

                    LogInfo( "Setting powerOn state to %u.", ( unsigned int ) ulNewState );
                    /* Set the new powerOn state. */
                    pxCtx->ulCurrentPowerOnState = ulNewState;

                    if( ulNewState == 1 )
                    {
                        HAL_GPIO_WritePin( LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET ); /* Turn the LED ON */
                    }
                    else
                    {
                        HAL_GPIO_WritePin( LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET ); /* Turn the LED off */
                    }
                }
            }
        }
    }
}

/*-----------------------------------------------------------*/

static void prvIncomingPublishUpdateAcceptedCallback( void * pvCtx,
                                                      MQTTPublishInfo_t * pxPublishInfo )
{
    char * pcOutValue = NULL;
    uint32_t ulOutValueLength = 0UL;
    uint32_t ulReceivedToken = 0UL;
    JSONStatus_t result = JSONSuccess;

    ShadowDeviceCtx_t * pxCtx = ( ShadowDeviceCtx_t * ) pvCtx;

    configASSERT( pvCtx != NULL );
    configASSERT( pxPublishInfo != NULL );
    configASSERT( pxPublishInfo->pPayload != NULL );

    LogDebug( "/update/accepted JSON payload: %.*s.",
              pxPublishInfo->payloadLength,
              ( const char * ) pxPublishInfo->pPayload );

    /* Handle the reported state with state change in /update/accepted topic.
     * Thus we will retrieve the client token from the JSON document to see if
     * it's the same one we sent with reported state on the /update topic.
     * The payload will look similar to this:
     *  {
     *      "state": {
     *          "reported": {
     *             "powerOn": 1
     *          }
     *      },
     *      "metadata": {
     *          "reported": {
     *              "powerOn": {
     *                  "timestamp": 1596573647
     *              }
     *          }
     *      },
     *      "version": 14698,
     *      "timestamp": 1596573647,
     *      "clientToken": "022485"
     *  }
     */

    /* Make sure the payload is a valid json document. */
    result = JSON_Validate( pxPublishInfo->pPayload,
                            pxPublishInfo->payloadLength );

    if( result != JSONSuccess )
    {
        LogError( "Invalid JSON document received!" );
    }
    else
    {
        /* Get clientToken from json documents. */
        result = JSON_Search( ( char * ) pxPublishInfo->pPayload,
                              pxPublishInfo->payloadLength,
                              "clientToken",
                              sizeof( "clientToken" ) - 1,
                              &pcOutValue,
                              ( size_t * ) &ulOutValueLength );
    }

    if( result != JSONSuccess )
    {
        LogDebug( "Ignoring publish on /update/accepted with no clientToken field." );
    }
    else
    {
        /* Convert the code to an unsigned integer value. */
        ulReceivedToken = ( uint32_t ) strtoul( pcOutValue, NULL, 10 );

        /* If we are waiting for a response, ulClientToken will be the token for the response
         * we are waiting for, else it will be 0. ulReceivedToken may not match if the response is
         * not for us or if it is is a response that arrived after we timed out
         * waiting for it.
         */
        if( ulReceivedToken != pxCtx->ulClientToken )
        {
            LogDebug( "Ignoring publish on /update/accepted with clientToken %lu.", ( unsigned long ) ulReceivedToken );
        }
        else
        {
            LogInfo( "Received accepted response for update with token %lu. ", ( unsigned long ) pxCtx->ulClientToken );

            /*  Obtain the accepted state from the response and update our last sent state. */
            result = JSON_Search( ( char * ) pxPublishInfo->pPayload,
                                  pxPublishInfo->payloadLength,
                                  "state.reported.powerOn",
                                  sizeof( "state.reported.powerOn" ) - 1,
                                  &pcOutValue,
                                  ( size_t * ) &ulOutValueLength );

            if( result != JSONSuccess )
            {
                LogError( "powerOn field not found in JSON document!" );
            }
            else
            {
                /* Convert the powerOn state value to an unsigned integer value and
                 * save the new last reported value*/
                pxCtx->ulReportedPowerOnState = ( uint32_t ) strtoul( pcOutValue, NULL, 10 );
            }

            /* Wake up the shadow task which is waiting for this response. */
            xTaskNotifyGive( pxCtx->xShadowDeviceTaskHandle );
        }
    }
}

/*-----------------------------------------------------------*/

static void prvIncomingPublishUpdateRejectedCallback( void * pvCtx,
                                                      MQTTPublishInfo_t * pxPublishInfo )
{
    JSONStatus_t result = JSONSuccess;
    char * pcOutValue = NULL;
    uint32_t ulOutValueLength = 0UL;
    uint32_t ulReceivedToken = 0UL;

    ShadowDeviceCtx_t * pxCtx = ( ShadowDeviceCtx_t * ) pvCtx;

    configASSERT( pvCtx != NULL );
    configASSERT( pxPublishInfo != NULL );
    configASSERT( pxPublishInfo->pPayload != NULL );

    LogDebug( "/update/rejected json payload: %.*s.",
              pxPublishInfo->payloadLength,
              ( const char * ) pxPublishInfo->pPayload );

    /* The payload will look similar to this:
     * {
     *    "code": error-code,
     *    "message": "error-message",
     *    "timestamp": timestamp,
     *    "clientToken": "token"
     * }
     */

    /* Make sure the payload is a valid json document. */
    result = JSON_Validate( pxPublishInfo->pPayload,
                            pxPublishInfo->payloadLength );

    if( result != JSONSuccess )
    {
        LogError( "Invalid JSON document received!" );
    }
    else
    {
        /* Get clientToken from json documents. */
        result = JSON_Search( ( char * ) pxPublishInfo->pPayload,
                              pxPublishInfo->payloadLength,
                              "clientToken",
                              sizeof( "clientToken" ) - 1,
                              &pcOutValue,
                              ( size_t * ) &ulOutValueLength );
    }

    if( result != JSONSuccess )
    {
        LogDebug( "Ignoring publish on /update/rejected with clientToken %lu.", ( unsigned long ) ulReceivedToken );
    }
    else
    {
        /* Convert the code to an unsigned integer value. */
        ulReceivedToken = ( uint32_t ) strtoul( pcOutValue, NULL, 10 );

        /* If we are waiting for a response, ulClientToken will be the token for the response
         * we are waiting for, else it will be 0. ulReceivedToken may not match if the response is
         * not for us or if it is is a response that arrived after we timed out
         * waiting for it.
         */
        if( ulReceivedToken != pxCtx->ulClientToken )
        {
            LogDebug( "Ignoring publish on /update/rejected with clientToken %lu.", ( unsigned long ) ulReceivedToken );
        }
        else
        {
            /*  Obtain the error code. */
            result = JSON_Search( ( char * ) pxPublishInfo->pPayload,
                                  pxPublishInfo->payloadLength,
                                  "code",
                                  sizeof( "code" ) - 1,
                                  &pcOutValue,
                                  ( size_t * ) &ulOutValueLength );

            if( result != JSONSuccess )
            {
                LogWarn( "Received rejected response for update with token %lu and no error code.", ( unsigned long ) pxCtx->ulClientToken );
            }
            else
            {
                LogWarn( "Received rejected response for update with token %lu and error code %.*s.", ( unsigned long ) pxCtx->ulClientToken,
                         ulOutValueLength,
                         pcOutValue );
            }

            /* Wake up the shadow task which is waiting for this response. */
            xTaskNotifyGive( pxCtx->xShadowDeviceTaskHandle );
        }
    }
}

/*-----------------------------------------------------------*/

void vShadowDeviceTask( void * pvParameters )
{
    bool xStatus = true;
    uint32_t ulNotificationValue;
    static MQTTPublishInfo_t xPublishInfo = { 0 };
    MQTTAgentCommandInfo_t xCommandParams = { 0 };
    MQTTStatus_t xCommandAdded;
    ShadowDeviceCtx_t xShadowCtx = { 0 };

    /* A buffer containing the update document. It has static duration to prevent
     * it from being placed on the call stack. */
    static char pcUpdateDocument[ shadowexampleSHADOW_REPORTED_JSON_LENGTH + 1 ] = { 0 };

    /* Remove compiler warnings about unused parameters. */
    ( void ) pvParameters;

    /* Record the handle of this task so that the callbacks can send a notification to this task. */
    xShadowCtx.xShadowDeviceTaskHandle = xTaskGetCurrentTaskHandle();

    /* Wait for MqttAgent to be ready. */
    vSleepUntilMQTTAgentReady();

    xShadowCtx.xAgentHandle = xGetMqttAgentHandle();

    xStatus = prvInitializeCtx( &xShadowCtx );

    /* Set up the MQTTAgentCommandInfo_t for the demo loop.
     * We do not need a completion callback here since for publishes, we expect to get a
     * response on the appropriate topics for accepted or rejected reports, and for pings
     * we do not care about the completion. */
    xCommandParams.blockTimeMs = shadowexampleMAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandParams.cmdCompleteCallback = NULL;

    /* Set up MQTTPublishInfo_t for the update reports. */
    xPublishInfo.qos = MQTTQoS1;
    xPublishInfo.pTopicName = xShadowCtx.pcTopicUpdate;
    xPublishInfo.topicNameLength = xShadowCtx.usTopicUpdateLen;
    xPublishInfo.pPayload = pcUpdateDocument;
    xPublishInfo.payloadLength = ( shadowexampleSHADOW_REPORTED_JSON_LENGTH + 1 );

    /* Wait for first mqtt connection */
    ( void ) xEventGroupWaitBits( xSystemEvents,
                                  EVT_MASK_MQTT_CONNECTED,
                                  pdFALSE,
                                  pdTRUE,
                                  portMAX_DELAY );

    if( xStatus == true )
    {
        /* Subscribe to Shadow topics. */
        xStatus = prvSubscribeToShadowUpdateTopics( &xShadowCtx );
    }

    if( xStatus == true )
    {
        for( ; ; )
        {
            if( xShadowCtx.ulCurrentPowerOnState == xShadowCtx.ulReportedPowerOnState )
            {
                LogDebug( "No change in powerOn state since last report. Current state is %u.", xShadowCtx.ulCurrentPowerOnState );
            }
            else
            {
                LogInfo( "PowerOn state is now %u. Sending new report.", ( unsigned int ) xShadowCtx.ulCurrentPowerOnState );

                /* Create a new client token and save it for use in the update accepted and rejected callbacks. */
                xShadowCtx.ulClientToken = ( xTaskGetTickCount() % 1000000 );

                /* Generate update report. */
                ( void ) memset( pcUpdateDocument,
                                 0x00,
                                 sizeof( pcUpdateDocument ) );

                snprintf( pcUpdateDocument,
                          shadowexampleSHADOW_REPORTED_JSON_LENGTH + 1,
                          shadowexampleSHADOW_REPORTED_JSON,
                          ( unsigned int ) xShadowCtx.ulCurrentPowerOnState,
                          ( long unsigned ) xShadowCtx.ulClientToken );

                /* Send update. */
                LogInfo( "Publishing to /update with following client token %lu.", ( long unsigned ) xShadowCtx.ulClientToken );
                LogDebug( "Publish content: %.*s", shadowexampleSHADOW_REPORTED_JSON_LENGTH, pcUpdateDocument );

                xCommandAdded = MQTTAgent_Publish( xShadowCtx.xAgentHandle,
                                                   &xPublishInfo,
                                                   &xCommandParams );

                if( xCommandAdded != MQTTSuccess )
                {
                    LogError( "Failed to publish report to shadow." );
                }
                else
                {
                    /* Wait for the response to our report. When the Device shadow service receives the request it will
                     * publish a response to  the /update/accepted or update/rejected */
                    ulNotificationValue = ulTaskNotifyTake( pdFALSE, pdMS_TO_TICKS( shadow_SIGNAL_TIMEOUT ) );

                    if( ulNotificationValue == 0 )
                    {
                        LogError( "Timed out waiting for response to report." );

                        /* If we time out waiting for a response and then the report is accepted, the
                         * state may be out of sync. Set the reported state as to ensure we resend the
                         * report. */
                        xShadowCtx.ulReportedPowerOnState = shadowexampleINVALID_POWERON_STATE;
                    }
                }

                /* Clear the client token */
                xShadowCtx.ulClientToken = 0;
            }

            LogDebug( "Sleeping until next update check." );
            vTaskDelay( pdMS_TO_TICKS( shadowMS_BETWEEN_REPORTS ) );
        }
    }
    else
    {
        LogError( "Terminating shadow_device task." );
        vTaskDelete( NULL );
    }
}

/*-----------------------------------------------------------*/
