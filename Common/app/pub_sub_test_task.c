/*
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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


/**
 * @brief A test application which loops through subscribing to a topic and publishing message
 * to a topic. This test application can be used with AWS IoT device advisor test suite to
 * verify that an application implemented using MQTT agent follows best practices in connecting
 * to AWS IoT core.
 */
/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* MQTT library includes. */
#include "core_mqtt.h"

/* MQTT agent include. */
#include "core_mqtt_agent.h"

/* MQTT agent task API. */
#include "mqtt_agent_task.h"

/* Subscription manager header include. */
#include "subscription_manager.h"


/**
 * @brief A test application which loops through subscribing to a topic and publishing message
 * to a topic. This test application can be used with AWS IoT device advisor test suite to
 * verify that an application implemented using MQTT agent follows best practices in connecting
 * to AWS IoT core.
 */
#define configMS_TO_WAIT_FOR_NOTIFICATION            ( 10000 )

/**
 * @brief Delay for the synchronous publisher task between publishes.
 */
#define configDELAY_BETWEEN_PUBLISH_OPERATIONS_MS    ( 2000U )

/**
 * @brief The maximum amount of time in milliseconds to wait for the commands
 * to be posted to the MQTT agent should the MQTT agent's command queue be full.
 * Tasks wait in the Blocked state, so don't use any CPU time.
 */
#define configMAX_COMMAND_SEND_BLOCK_TIME_MS         ( 500 )

/**
 * @brief Size of statically allocated buffers for holding payloads.
 */
#define confgPAYLOAD_BUFFER_LENGTH                   ( 100 )

/**
 * @brief Format of topic used to publish outgoing messages.
 */
#define configPUBLISH_TOPIC_FORMAT                   "mqtt_test/outgoing"

/**
 * @brief Size of the static buffer to hold the topic name.
 */
#define configPUBLISH_TOPIC_BUFFER_LENGTH            ( sizeof( configPUBLISH_TOPIC_FORMAT ) - 1 )


/**
 * @brief Format of topic used to subscribe to incoming messages.
 *
 */
#define configSUBSCRIBE_TOPIC_FORMAT           "mqtt_test/incoming"

/**
 * @brief Size of the static buffer to hold the topic name.
 */
#define configSUBSCRIBE_TOPIC_BUFFER_LENGTH    ( sizeof( configSUBSCRIBE_TOPIC_FORMAT ) - 1 )

/*-----------------------------------------------------------*/

/**
 * @brief Defines the structure to use as the command callback context in this
 * demo.
 */
struct MQTTAgentCommandContext
{
    TaskHandle_t xTaskToNotify;
    void * pArgs;
};

/*-----------------------------------------------------------*/

MQTTAgentHandle_t xMQTTAgentHandle = NULL;

/*-----------------------------------------------------------*/

/**
 * @brief Passed into MQTTAgent_Publish() as the callback to execute when the
 * broker ACKs the PUBLISH message.  Its implementation sends a notification
 * to the task that called MQTTAgent_Publish() to let the task know the
 * PUBLISH operation completed.  It also sets the xReturnStatus of the
 * structure passed in as the command's context to the value of the
 * xReturnStatus parameter - which enables the task to check the status of the
 * operation.
 *
 * See https://freertos.org/mqtt/mqtt-agent-demo.html#example_mqtt_api_call
 *
 * @param[in] pxCommandContext Context of the initial command.
 * @param[in].xReturnStatus The result of the command.
 */
static void prvPublishCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                       MQTTAgentReturnInfo_t * pxReturnInfo );

/**
 * @brief Passed into MQTTAgent_Subscribe() as the callback to execute when
 * there is an incoming publish on the topic being subscribed to.  Its
 * implementation just logs information about the incoming publish including
 * the publish messages source topic and payload.
 *
 * See https://freertos.org/mqtt/mqtt-agent-demo.html#example_mqtt_api_call
 *
 * @param[in] pvIncomingPublishCallbackContext Context of the initial command.
 * @param[in] pxPublishInfo Deserialized publish.
 */
static void prvIncomingPublishCallback( void * pvIncomingPublishCallbackContext,
                                        MQTTPublishInfo_t * pxPublishInfo );

/**
 * @brief Subscribe to the topic the demo task will also publish to - that
 * results in all outgoing publishes being published back to the task
 * (effectively echoed back).
 *
 * @param[in] xQoS The quality of service (QoS) to use.  Can be zero or one
 * for all MQTT brokers.  Can also be QoS2 if supported by the broker.  AWS IoT
 * does not support QoS2.
 */
static MQTTStatus_t prvSubscribeToTopic( MQTTQoS_t xQoS,
                                         char * pcTopicFilter );



/**
 * @brief Publishes the given payload using the given qos to the topic provided.
 *
 * Function queues a publish command with the MQTT agent and waits for response. For
 * Qos0 publishes command is successful when the message is sent out of network. For Qos1
 * publishes, the command succeeds once a puback is received. If publish is unsuccessful, the function
 * retries the publish for a configure number of tries.
 *
 * @param[in] xQoS The quality of service (QoS) to use.  Can be zero or one
 * for all MQTT brokers.  Can also be QoS2 if supported by the broker.  AWS IoT
 * does not support QoS2.
 * @param[in] pcTopic NULL terminated topic string to which message is published.
 * @param[in] pucPayload The payload blob to be published.
 * @param[in] xPayloadLength Length of the payload blob to be published.
 */
static MQTTStatus_t prvPublishToTopic( MQTTQoS_t xQoS,
                                       char * pcTopic,
                                       uint8_t * pucPayload,
                                       size_t xPayloadLength );

/**
 * @brief The function that implements the task demonstrated by this file.
 *
 * @param pvParameters The parameters to the task.
 */
void vSubscribePublishTestTask( void * pvParameters );

/*-----------------------------------------------------------*/

/**
 * @brief The MQTT agent manages the MQTT contexts.  This set the handle to the
 * context used by this demo.
 */
extern MQTTAgentContext_t xGlobalMqttAgentContext;

/*-----------------------------------------------------------*/

static void prvPublishCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                       MQTTAgentReturnInfo_t * pxReturnInfo )
{
    if( pxCommandContext->xTaskToNotify != NULL )
    {
        xTaskNotify( pxCommandContext->xTaskToNotify,
                     pxReturnInfo->returnCode,
                     eSetValueWithOverwrite );
    }
}

/*-----------------------------------------------------------*/

static void prvIncomingPublishCallback( void * pvIncomingPublishCallbackContext,
                                        MQTTPublishInfo_t * pxPublishInfo )
{
    static char cTerminatedString[ confgPAYLOAD_BUFFER_LENGTH ];

    ( void ) pvIncomingPublishCallbackContext;

    /* Create a message that contains the incoming MQTT payload to the logger,
     * terminating the string first. */
    if( pxPublishInfo->payloadLength < confgPAYLOAD_BUFFER_LENGTH )
    {
        memcpy( ( void * ) cTerminatedString, pxPublishInfo->pPayload, pxPublishInfo->payloadLength );
        cTerminatedString[ pxPublishInfo->payloadLength ] = 0x00;
    }
    else
    {
        memcpy( ( void * ) cTerminatedString, pxPublishInfo->pPayload, confgPAYLOAD_BUFFER_LENGTH );
        cTerminatedString[ confgPAYLOAD_BUFFER_LENGTH - 1 ] = 0x00;
    }

    LogInfo( ( "Received incoming publish message %s", cTerminatedString ) );
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvSubscribeToTopic( MQTTQoS_t xQoS,
                                         char * pcTopicFilter )
{
    MQTTStatus_t xMQTTStatus;

    /* Loop in case the queue used to communicate with the MQTT agent is full and
     * attempts to post to it time out.  The queue will not become full if the
     * priority of the MQTT agent task is higher than the priority of the task
     * calling this function. */
    do
    {
        xMQTTStatus = MqttAgent_SubscribeSync( xMQTTAgentHandle,
                                               pcTopicFilter,
                                               xQoS,
                                               prvIncomingPublishCallback,
                                               NULL );

        if( xMQTTStatus != MQTTSuccess )
        {
            LogError( ( "Failed to SUBSCRIBE to topic with error = %u.",
                        xMQTTStatus ) );
        }
        else
        {
            LogInfo( ( "Subscribed to topic %.*s.\n\n",
                       strlen( pcTopicFilter ),
                       pcTopicFilter ) );
        }
    } while( xMQTTStatus != MQTTSuccess );

    return xMQTTStatus;
}
/*-----------------------------------------------------------*/


static MQTTStatus_t prvPublishToTopic( MQTTQoS_t xQoS,
                                       char * pcTopic,
                                       uint8_t * pucPayload,
                                       size_t xPayloadLength )
{
    MQTTPublishInfo_t xPublishInfo = { 0UL };
    MQTTAgentCommandContext_t xCommandContext = { 0 };
    MQTTStatus_t xMQTTStatus;
    BaseType_t xNotifyStatus;
    MQTTAgentCommandInfo_t xCommandParams = { 0UL };
    uint32_t ulNotifiedValue = 0U;

    /* Create a unique number of the subscribe that is about to be sent.  The number
     * is used as the command context and is sent back to this task as a notification
     * in the callback that executed upon receipt of the subscription acknowledgment.
     * That way this task can match an acknowledgment to a subscription. */
    xTaskNotifyStateClear( NULL );

    /* Configure the publish operation. */
    xPublishInfo.qos = xQoS;
    xPublishInfo.pTopicName = pcTopic;
    xPublishInfo.topicNameLength = ( uint16_t ) strlen( pcTopic );
    xPublishInfo.pPayload = pucPayload;
    xPublishInfo.payloadLength = xPayloadLength;

    xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();

    xCommandParams.blockTimeMs = configMAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandParams.cmdCompleteCallback = prvPublishCommandCallback;
    xCommandParams.pCmdCompleteCallbackContext = &xCommandContext;

    /* Loop in case the queue used to communicate with the MQTT agent is full and
     * attempts to post to it time out.  The queue will not become full if the
     * priority of the MQTT agent task is higher than the priority of the task
     * calling this function. */
    do
    {
        xMQTTStatus = MQTTAgent_Publish( xMQTTAgentHandle,
                                         &xPublishInfo,
                                         &xCommandParams );

        if( xMQTTStatus == MQTTSuccess )
        {
            /* Wait for this task to get notified, passing out the value it gets  notified with. */
            xNotifyStatus = xTaskNotifyWait( 0,
                                             0,
                                             &ulNotifiedValue,
                                             portMAX_DELAY );

            if( xNotifyStatus == pdTRUE )
            {
                xMQTTStatus = ( MQTTStatus_t ) ( ulNotifiedValue );
            }
            else
            {
                xMQTTStatus = MQTTRecvFailed;
            }
        }
    } while( xMQTTStatus != MQTTSuccess );

    return xMQTTStatus;
}

/*-----------------------------------------------------------*/


void vSubscribePublishTestTask( void * pvParameters )
{
    char cPayloadBuf[ confgPAYLOAD_BUFFER_LENGTH ];
    size_t xPayloadLength;
    uint32_t ulPublishCount = 0U, ulSuccessCount = 0U, ulFailCount = 0U;
    BaseType_t xStatus = pdPASS;
    MQTTStatus_t xMQTTStatus;
    MQTTQoS_t xQoS;
    TickType_t xTicksToDelay;

    ( void ) pvParameters;

    vSleepUntilMQTTAgentReady();

    xMQTTAgentHandle = xGetMqttAgentHandle();
    configASSERT( xMQTTAgentHandle != NULL );

    vSleepUntilMQTTAgentConnected();

    LogInfo( ( "MQTT Agent is connected. Starting the publish subscribe task. " ) );

    if( xStatus == pdPASS )
    {
        xMQTTStatus = prvSubscribeToTopic( MQTTQoS1,
                                           configSUBSCRIBE_TOPIC_FORMAT );

        if( xMQTTStatus != MQTTSuccess )
        {
            LogError( ( "Failed to subscribe to topic: %s.", configSUBSCRIBE_TOPIC_FORMAT ) );
            xStatus = pdFAIL;
        }
    }

    if( xStatus == pdPASS )
    {
        /* Loop through infinitely. */
        for( ; ; )
        {
            /* Have different tasks use different QoS.  0 and 1.  2 can also be used
             * if supported by the broker. */
            xQoS = ( MQTTQoS_t ) ( ( ulPublishCount + 1 ) % 2UL );


            /* Create a payload to send with the publish message.  This contains
             * the task name and an incrementing number. */
            xPayloadLength = snprintf( cPayloadBuf,
                                       confgPAYLOAD_BUFFER_LENGTH,
                                       "Test message %lu",
                                       ( ulPublishCount + 1 ) );

            /* Assert if the buffer length is large enough to hold the message. */
            configASSERT( xPayloadLength <= confgPAYLOAD_BUFFER_LENGTH );

            LogInfo( ( "Sending publish message to topic: %s with qos: %d, message : %*s",
                       configPUBLISH_TOPIC_FORMAT,
                       xQoS,
                       xPayloadLength,
                       ( char * ) cPayloadBuf ) );

            xMQTTStatus = prvPublishToTopic( xQoS,
                                             configPUBLISH_TOPIC_FORMAT,
                                             ( uint8_t * ) cPayloadBuf,
                                             xPayloadLength );

            if( xMQTTStatus == MQTTSuccess )
            {
                ulSuccessCount++;
                LogInfo( ( "Successfully sent QoS %u publish to topic: %s (PassCount:%d, FailCount:%d).",
                           xQoS,
                           configPUBLISH_TOPIC_FORMAT,
                           ulSuccessCount,
                           ulFailCount ) );
            }
            else
            {
                ulFailCount++;
                LogError( ( "Timed out while sending QoS %u publish to topic: %s (PassCount:%d, FailCount: %d)",
                            xQoS,
                            configPUBLISH_TOPIC_FORMAT,
                            ulSuccessCount,
                            ulFailCount ) );
            }

            /* Add a little randomness into the delay so the tasks don't remain
             * in lockstep. */
            xTicksToDelay = pdMS_TO_TICKS( configDELAY_BETWEEN_PUBLISH_OPERATIONS_MS ) +
                            ( xTaskGetTickCount() % 0xff );
            vTaskDelay( xTicksToDelay );

            ulPublishCount++;
        }
    }

    vTaskDelete( NULL );
}
