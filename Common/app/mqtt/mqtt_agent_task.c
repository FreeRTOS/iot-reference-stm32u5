/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 * Derived from Lab-Project-coreMQTT-Agent 201215
 *
 */

#include "logging_levels.h"
#define LOG_LEVEL    LOG_INFO
#include "logging.h"

/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "event_groups.h"

#include "kvstore.h"
#include "mqtt_metrics.h"

/* MQTT library includes. */
#include "core_mqtt.h"

#include "core_mqtt_serializer.h"

/* MQTT agent include. */
#include "core_mqtt_agent.h"
#include "core_mqtt_agent_message_interface.h"

/* MQTT Agent ports. */
#include "freertos_command_pool.h"

/* Exponential backoff retry include. */
#include "backoff_algorithm.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

#include "mbedtls_transport.h"
#include "sys_evt.h"

/*-----------------------------------------------------------*/

/**
 * @brief Timeout for receiving CONNACK after sending an MQTT CONNECT packet.
 * Defined in milliseconds.
 */
#define CONNACK_RECV_TIMEOUT_MS     ( 2000U )

/**
 * @brief The minimum back-off delay to use for network operation retry
 * attempts.
 */
#define RETRY_BACKOFF_BASE          ( 10U )

/**
 * @brief The maximum back-off delay for retrying failed operation
 *  with server.
 */
#define RETRY_MAX_BACKOFF_DELAY     ( 5U * 60U )

/**
 * @brief Multiplier to apply to the bacoff delay to convert arbitrary units to milliseconds.
 */
#define RETRY_BACKOFF_MULTIPLIER    ( 100U )

static_assert( RETRY_BACKOFF_BASE < UINT16_MAX );
static_assert( RETRY_MAX_BACKOFF_DELAY < UINT16_MAX );
static_assert( ( ( uint64_t ) RETRY_BACKOFF_MULTIPLIER * ( uint64_t ) RETRY_MAX_BACKOFF_DELAY ) < UINT32_MAX );

/**
 * @brief The maximum time interval in seconds which is allowed to elapse
 *  between two Control Packets.
 *
 *  It is the responsibility of the Client to ensure that the interval between
 *  Control Packets being sent does not exceed the this Keep Alive value. In the
 *  absence of sending any other Control Packets, the Client MUST send a
 *  PINGREQ Packet.
 */
#define KEEP_ALIVE_INTERVAL_S                 ( 1200U )

#define MQTT_AGENT_NOTIFY_IDX                 ( 3U )

#define MQTT_AGENT_NOTIFY_FLAG_SOCKET_RECV    ( 1U << 31 )
#define MQTT_AGENT_NOTIFY_FLAG_M_QUEUE        ( 1U << 30 )

/**
 * @brief Socket send and receive timeouts to use.
 */
#define SEND_TIMEOUT_MS                       ( 2000U )

#define AGENT_READY_EVT_MASK                  ( 1U )

#define MUTEX_IS_OWNED( xHandle )    ( xTaskGetCurrentTaskHandle() == xSemaphoreGetMutexHolder( xHandle ) )

struct MQTTAgentMessageContext
{
    QueueHandle_t xQueue;
    TaskHandle_t xAgentTaskHandle;
};

typedef struct MQTTAgentSubscriptionManagerCtx
{
    MQTTSubscribeInfo_t pxSubscriptions[ MQTT_AGENT_MAX_SUBSCRIPTIONS ];
    MQTTSubAckStatus_t pxSubAckStatus[ MQTT_AGENT_MAX_SUBSCRIPTIONS ];
    uint32_t pulSubCbCount[ MQTT_AGENT_MAX_SUBSCRIPTIONS ];
    SubCallbackElement_t pxCallbacks[ MQTT_AGENT_MAX_CALLBACKS ];

    size_t uxSubscriptionCount;
    size_t uxCallbackCount;
    MQTTAgentSubscribeArgs_t xInitialSubscribeArgs;

    SemaphoreHandle_t xMutex;
} SubMgrCtx_t;


typedef struct MQTTAgentTaskCtx
{
    MQTTAgentContext_t xAgentContext;

    MQTTFixedBuffer_t xNetworkFixedBuffer;
    TransportInterface_t xTransport;

    MQTTAgentMessageInterface_t xMessageInterface;
    MQTTAgentMessageContext_t xAgentMessageCtx;

    SubMgrCtx_t xSubMgrCtx;

    MQTTConnectInfo_t xConnectInfo;
    char * pcMqttEndpoint;
    size_t uxMqttEndpointLen;
    uint32_t ulMqttPort;
} MQTTAgentTaskCtx_t;

/* ALPN protocols must be a NULL-terminated list of strings. */
static const char * pcAlpnProtocols[] = { AWS_IOT_MQTT_ALPN, NULL };

static MQTTAgentHandle_t xDefaultInstanceHandle = NULL;

/*-----------------------------------------------------------*/

/**
 * @brief Fan out the incoming publishes to the callbacks registered by different
 * tasks. If there are no callbacks registered for the incoming publish, it will be
 * passed to the unsolicited publish handler.
 *
 * @param[in] pMqttAgentContext Agent context.
 * @param[in] packetId Packet ID of publish.
 * @param[in] pxPublishInfo Info of incoming publish.
 */
static void prvIncomingPublishCallback( MQTTAgentContext_t * pMqttAgentContext,
                                        uint16_t packetId,
                                        MQTTPublishInfo_t * pxPublishInfo );

/**
 * @brief Function to attempt to resubscribe to the topics already present in the
 * subscription list.
 *
 * This function will be invoked when this demo requests the broker to
 * reestablish the session and the broker cannot do so. This function will
 * enqueue commands to the MQTT Agent queue and will be processed once the
 * command loop starts.
 *
 * @return `MQTTSuccess` if adding subscribes to the command queue succeeds, else
 * appropriate error code from MQTTAgent_Subscribe.
 * */
static MQTTStatus_t prvHandleResubscribe( MQTTAgentContext_t * pxMqttAgentCtx,
                                          SubMgrCtx_t * pxCtx );

/*-----------------------------------------------------------*/

/**
 * @brief The timer query function provided to the MQTT context.
 *
 * @return Time in milliseconds.
 */
static uint32_t prvGetTimeMs( void );

/*-----------------------------------------------------------*/

/**
 * @brief Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the chances
 * of overflow for the 32 bit unsigned integer used for holding the timestamp.
 */
static uint32_t ulGlobalEntryTimeMs;

/*-----------------------------------------------------------*/

static inline BaseType_t xLockSubCtx( SubMgrCtx_t * pxSubCtx )
{
    BaseType_t xResult = pdFALSE;

    configASSERT( pxSubCtx );
    configASSERT( pxSubCtx->xMutex );

    configASSERT_CONTINUE( !MUTEX_IS_OWNED( pxSubCtx->xMutex ) );

    LogDebug( "Waiting for Mutex." );
    xResult = xSemaphoreTake( pxSubCtx->xMutex, portMAX_DELAY );

    if( xResult )
    {
        LogDebug( ">>>> Mutex wait complete." );
    }
    else
    {
        LogError( "**** Mutex request failed, xResult=%d.", xResult );
    }

    return xResult;
}

/*-----------------------------------------------------------*/

static inline BaseType_t xUnlockSubCtx( SubMgrCtx_t * pxSubCtx )
{
    BaseType_t xResult = pdFALSE;

    configASSERT( pxSubCtx );
    configASSERT( pxSubCtx->xMutex );
    configASSERT_CONTINUE( MUTEX_IS_OWNED( pxSubCtx->xMutex ) );

    xResult = xSemaphoreGive( pxSubCtx->xMutex );

    if( xResult )
    {
        LogDebug( "<<<< Mutex Give." );
    }
    else
    {
        LogError( "**** Mutex Give request failed, xResult=%d.", xResult );
    }

    return xResult;
}

/*-----------------------------------------------------------*/
void vSleepUntilMQTTAgentReady( void )
{
    configASSERT( xSystemEvents != NULL );

    while( 1 )
    {
        EventBits_t uxEvents = xEventGroupWaitBits( xSystemEvents,
                                                    EVT_MASK_MQTT_INIT,
                                                    pdFALSE,
                                                    pdTRUE,
                                                    portMAX_DELAY );

        if( uxEvents & EVT_MASK_MQTT_INIT )
        {
            break;
        }
    }

    while( xGetMqttAgentHandle() == NULL )
    {
        vTaskDelay( 10 );
    }
}

/*-----------------------------------------------------------*/

void vSleepUntilMQTTAgentConnected( void )
{
    configASSERT( xSystemEvents != NULL );

    while( 1 )
    {
        EventBits_t uxEvents = xEventGroupWaitBits( xSystemEvents,
                                                    EVT_MASK_MQTT_CONNECTED,
                                                    pdFALSE,
                                                    pdTRUE,
                                                    portMAX_DELAY );

        if( uxEvents & EVT_MASK_MQTT_CONNECTED )
        {
            break;
        }
    }
}

/*-----------------------------------------------------------*/

bool xIsMqttAgentConnected( void )
{
    EventBits_t uxEvents = xEventGroupWaitBits( xSystemEvents,
                                                EVT_MASK_MQTT_CONNECTED,
                                                pdFALSE,
                                                pdTRUE,
                                                0 );

    return( ( bool ) ( uxEvents & EVT_MASK_MQTT_CONNECTED ) );
}

/*-----------------------------------------------------------*/

static inline void prvUpdateCallbackRefs( SubCallbackElement_t * pxCallbacksList,
                                          MQTTSubscribeInfo_t * pxSubList,
                                          size_t uxOldIdx,
                                          size_t uxNewIdx )
{
    configASSERT( pxCallbacksList );
    configASSERT( pxSubList );
    configASSERT( uxOldIdx < MQTT_AGENT_MAX_SUBSCRIPTIONS );
    configASSERT( uxNewIdx < MQTT_AGENT_MAX_SUBSCRIPTIONS );

    for( size_t uxIdx = 0; uxIdx < MQTT_AGENT_MAX_CALLBACKS; uxIdx++ )
    {
        if( pxCallbacksList[ uxIdx ].pxSubInfo == &( pxSubList[ uxOldIdx ] ) )
        {
            pxCallbacksList[ uxIdx ].pxSubInfo = &( pxSubList[ uxNewIdx ] );
        }
    }
}

/*-----------------------------------------------------------*/

static inline void prvCompressSubscriptionList( MQTTSubscribeInfo_t * pxSubList,
                                                SubCallbackElement_t * pxCallbacksList,
                                                size_t * puxSubCount )
{
    size_t uxLastOccupiedIndex = 0;
    size_t uxSubCount = 0;

    configASSERT( pxSubList );
    configASSERT( pxCallbacksList );

    for( size_t uxIdx = 0U; uxIdx < MQTT_AGENT_MAX_SUBSCRIPTIONS; uxIdx++ )
    {
        if( ( pxSubList[ uxIdx ].pTopicFilter == NULL ) &&
            ( pxSubList[ uxIdx ].topicFilterLength == 0 ) &&
            ( uxLastOccupiedIndex < MQTT_AGENT_MAX_SUBSCRIPTIONS ) )
        {
            if( uxLastOccupiedIndex <= uxIdx )
            {
                uxLastOccupiedIndex = uxIdx + 1;
            }

            /* Iterate over remainder of list for occupied spots */
            for( ; uxLastOccupiedIndex < MQTT_AGENT_MAX_CALLBACKS; uxLastOccupiedIndex++ )
            {
                if( pxSubList[ uxLastOccupiedIndex ].topicFilterLength != 0 )
                {
                    /* Move new item into place */
                    pxSubList[ uxIdx ] = pxSubList[ uxLastOccupiedIndex ];

                    /* Clear old location */
                    memset( &( pxSubList[ uxLastOccupiedIndex ] ), 0, sizeof( MQTTSubscribeInfo_t ) );

                    prvUpdateCallbackRefs( pxCallbacksList, pxSubList, uxLastOccupiedIndex, uxIdx );

                    /* Increment count of active subscriptions */
                    uxSubCount++;

                    /* Increment counter so we don't visit this location again */
                    uxLastOccupiedIndex++;

                    break;
                }
            }
        }
        else if( ( pxSubList[ uxIdx ].pTopicFilter != NULL ) &&
                 ( pxSubList[ uxIdx ].topicFilterLength != 0 ) )
        {
            /* Increment count of active subscriptions */
            uxSubCount++;
        }
        else /*  pxSubList[ uxIdx ].topicFilterLength == 0 */
        {
            /* Continue iterating */
        }
    }

    *puxSubCount = uxSubCount;
}

/*-----------------------------------------------------------*/

static inline void prvCompressCallbackList( SubCallbackElement_t * pxCallbackList,
                                            size_t * puxCallbackCount )
{
    size_t uxLastOccupiedIndex = 0;
    size_t uxCallbackCount = 0;

    configASSERT( pxCallbackList );
    configASSERT( puxCallbackCount );

    for( size_t uxIdx = 0U; uxIdx < MQTT_AGENT_MAX_CALLBACKS; uxIdx++ )
    {
        if( ( pxCallbackList[ uxIdx ].pxSubInfo == NULL ) &&     /* Current slot at uxIdx is Empty */
            ( uxLastOccupiedIndex < MQTT_AGENT_MAX_CALLBACKS ) ) /* There may be occupied slots after the current one */
        {
            /* uxLastOccupiedIndex is always > uxIdx */
            if( uxLastOccupiedIndex <= uxIdx )
            {
                uxLastOccupiedIndex = uxIdx + 1;
            }

            /* Iterate over remainder of list for occupied spots */
            for( ; uxLastOccupiedIndex < MQTT_AGENT_MAX_CALLBACKS; uxLastOccupiedIndex++ )
            {
                if( pxCallbackList[ uxLastOccupiedIndex ].pxSubInfo != NULL )
                {
                    configASSERT( uxLastOccupiedIndex > uxIdx );

                    /* Move context from uxLastOccupiedIndex to uxIdx */
                    pxCallbackList[ uxIdx ] = pxCallbackList[ uxLastOccupiedIndex ];

                    /* Clear old context location */
                    memset( &( pxCallbackList[ uxLastOccupiedIndex ] ), 0, sizeof( SubCallbackElement_t ) );

                    pxCallbackList[ uxLastOccupiedIndex ].pxIncomingPublishCallback = NULL;
                    pxCallbackList[ uxLastOccupiedIndex ].pvIncomingPublishCallbackContext = NULL;
                    pxCallbackList[ uxLastOccupiedIndex ].xTaskHandle = NULL;
                    pxCallbackList[ uxLastOccupiedIndex ].pxSubInfo = NULL;

                    /* Increment count of active callbacks */
                    uxCallbackCount++;

                    /* Increment counter so we don't visit this empty location again */
                    uxLastOccupiedIndex++;
                    break;
                }
            }
        }
        else if( pxCallbackList[ uxIdx ].pxSubInfo != NULL )
        {
            /* Increment count of valid contexts */
            uxCallbackCount++;
        }
        else /* pxCallbackList[ uxIdx ].pxSubInfo == NULL */
        {
            /* Continue iterating, ignore empty slot. */
        }
    }

    *puxCallbackCount = uxCallbackCount;
}

/*-----------------------------------------------------------*/

static void prvSocketRecvReadyCallback( void * pvCtx )
{
    MQTTAgentMessageContext_t * pxMsgCtx = ( MQTTAgentMessageContext_t * ) pvCtx;

    if( pxMsgCtx )
    {
        ( void ) xTaskNotifyIndexed( pxMsgCtx->xAgentTaskHandle,
                                     MQTT_AGENT_NOTIFY_IDX,
                                     MQTT_AGENT_NOTIFY_FLAG_SOCKET_RECV,
                                     eSetBits );
    }
}

/*-----------------------------------------------------------*/

static bool prvAgentMessageSend( MQTTAgentMessageContext_t * pxMsgCtx,
                                 MQTTAgentCommand_t * const * pxCommandToSend,
                                 uint32_t blockTimeMs )
{
    BaseType_t xQueueStatus = pdFAIL;

    if( pxMsgCtx && pxCommandToSend )
    {
        xQueueStatus = xQueueSendToBack( pxMsgCtx->xQueue, pxCommandToSend, pdMS_TO_TICKS( blockTimeMs ) );

        /* Notify the agent that a message is waiting */
        if( pxMsgCtx->xAgentTaskHandle )
        {
            ( void ) xTaskNotifyIndexed( pxMsgCtx->xAgentTaskHandle,
                                         MQTT_AGENT_NOTIFY_IDX,
                                         MQTT_AGENT_NOTIFY_FLAG_M_QUEUE,
                                         eSetBits );
        }
    }

    return ( bool ) xQueueStatus;
}

/*-----------------------------------------------------------*/

static bool prvAgentMessageReceive( MQTTAgentMessageContext_t * pxMsgCtx,
                                    MQTTAgentCommand_t ** ppxReceivedCommand,
                                    uint32_t blockTimeMs )
{
    BaseType_t xQueueStatus = pdFAIL;
    uint32_t ulNotifyValue = 0;

    if( pxMsgCtx && ppxReceivedCommand )
    {
        if( xTaskNotifyWaitIndexed( MQTT_AGENT_NOTIFY_IDX,
                                    0x0,
                                    0xFFFFFFFF,
                                    &ulNotifyValue,
                                    pdMS_TO_TICKS( blockTimeMs ) ) )
        {
            /* Prioritize processing incoming network packets over local requests */
            if( ulNotifyValue & MQTT_AGENT_NOTIFY_FLAG_SOCKET_RECV )
            {
                *ppxReceivedCommand = NULL;
            }
            else
            {
                xQueueStatus = xQueueReceive( pxMsgCtx->xQueue, ppxReceivedCommand, 0 );
            }
        }
    }

    return ( bool ) xQueueStatus;
}


/*-----------------------------------------------------------*/

static void prvResubscribeCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                           MQTTAgentReturnInfo_t * pxReturnInfo )
{
    SubMgrCtx_t * pxCtx = ( SubMgrCtx_t * ) pxCommandContext;

    configASSERT( pxCommandContext != NULL );
    configASSERT( pxReturnInfo != NULL );
    configASSERT( MUTEX_IS_OWNED( pxCtx->xMutex ) );

    /* Ignore pxReturnInfo->returnCode */

    for( uint32_t ulSubIdx = 0; ulSubIdx < pxCtx->uxSubscriptionCount; ulSubIdx++ )
    {
        /* Update cached SubAck status */
        pxCtx->pxSubAckStatus[ ulSubIdx ] = pxReturnInfo->pSubackCodes[ ulSubIdx ];

        if( pxReturnInfo->pSubackCodes[ ulSubIdx ] == MQTTSubAckFailure )
        {
            MQTTSubscribeInfo_t * const pxSubInfo = &( pxCtx->pxSubscriptions[ ulSubIdx ] );

            LogError( "Failed to re-subscribe to topic filter \"%.*s\".",
                      pxSubInfo->topicFilterLength,
                      pxSubInfo->pTopicFilter );

            for( uint32_t ulCbIdx = 0; ulCbIdx < MQTT_AGENT_MAX_CALLBACKS; ulCbIdx++ )
            {
                SubCallbackElement_t * const pxCbInfo = &( pxCtx->pxCallbacks[ ulCbIdx ] );

                if( ( pxCbInfo->pxSubInfo == pxSubInfo ) &&
                    ( pxCbInfo->xTaskHandle != NULL ) )
                {
                    LogWarn( "Detected orphaned callback for task: %s due to failed re-subscribe operation.",
                             pcTaskGetName( pxCbInfo->xTaskHandle ) );
                }
            }
        }
    }

    ( void ) xUnlockSubCtx( pxCtx );
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvHandleResubscribe( MQTTAgentContext_t * pxMqttAgentCtx,
                                          SubMgrCtx_t * pxCtx )
{
    MQTTStatus_t xStatus = MQTTBadParameter;

    configASSERT( pxCtx );
    configASSERT( pxCtx->xMutex );
    configASSERT( MUTEX_IS_OWNED( pxCtx->xMutex ) );

    prvCompressSubscriptionList( pxCtx->pxSubscriptions,
                                 pxCtx->pxCallbacks,
                                 &( pxCtx->uxSubscriptionCount ) );

    if( ( xStatus == MQTTSuccess ) && ( pxCtx->uxSubscriptionCount > 0U ) )
    {
        MQTTAgentCommandInfo_t xCommandParams =
        {
            .blockTimeMs                 = 0U,
            .cmdCompleteCallback         = prvResubscribeCommandCallback,
            .pCmdCompleteCallbackContext = ( void * ) pxCtx,
        };

        pxCtx->xInitialSubscribeArgs.pSubscribeInfo = pxCtx->pxSubscriptions;
        pxCtx->xInitialSubscribeArgs.numSubscriptions = pxCtx->uxSubscriptionCount;

        /* Enqueue the subscribe command */
        xStatus = MQTTAgent_Subscribe( pxMqttAgentCtx,
                                       &( pxCtx->xInitialSubscribeArgs ),
                                       &xCommandParams );

        /* prvResubscribeCommandCallback handles giving the mutex */

        if( xStatus != MQTTSuccess )
        {
            LogError( "Failed to enqueue the MQTT subscribe command. xStatus=%s.",
                      MQTT_Status_strerror( xStatus ) );
        }
    }
    else
    {
        ( void ) xUnlockSubCtx( pxCtx );

        /* Mark the resubscribe as success if there is nothing to be subscribed to. */
        xStatus = MQTTSuccess;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

static inline bool prvMatchTopic( MQTTSubscribeInfo_t * pxSubscribeInfo,
                                  const char * pcTopicName,
                                  const uint16_t usTopicLen )
{
    MQTTStatus_t xStatus = MQTTSuccess;
    bool isMatched = false;

    xStatus = MQTT_MatchTopic( pcTopicName,
                               usTopicLen,
                               pxSubscribeInfo->pTopicFilter,
                               pxSubscribeInfo->topicFilterLength,
                               &isMatched );
    return ( xStatus == MQTTSuccess ) && isMatched;
}

/*-----------------------------------------------------------*/

static inline bool prvMatchCbCtx( SubCallbackElement_t * pxCbCtx,
                                  MQTTSubscribeInfo_t * pxSubInfo,
                                  IncomingPubCallback_t pxCallback,
                                  void * pvCallbackCtx )
{
    return( pxCbCtx->pxSubInfo == pxSubInfo &&
            pxCbCtx->pvIncomingPublishCallbackContext == pvCallbackCtx &&
            pxCbCtx->pxIncomingPublishCallback == pxCallback &&
            pxCbCtx->xTaskHandle == xTaskGetCurrentTaskHandle() );
}

/*-----------------------------------------------------------*/

static void prvIncomingPublishCallback( MQTTAgentContext_t * pMqttAgentContext,
                                        uint16_t packetId,
                                        MQTTPublishInfo_t * pxPublishInfo )
{
    SubMgrCtx_t * pxCtx = NULL;
    bool xPublishHandled = false;

    ( void ) packetId;

    configASSERT( pMqttAgentContext );
    configASSERT( pMqttAgentContext->pIncomingCallbackContext );
    configASSERT( pxPublishInfo );

    pxCtx = ( SubMgrCtx_t * ) pMqttAgentContext->pIncomingCallbackContext;

    if( xLockSubCtx( pxCtx ) )
    {
        /* Iterate over pxCtx->pxCallbacks list */
        for( uint32_t ulCbIdx = 0; ulCbIdx < MQTT_AGENT_MAX_CALLBACKS; ulCbIdx++ )
        {
            SubCallbackElement_t * const pxCallback = &( pxCtx->pxCallbacks[ ulCbIdx ] );
            MQTTSubscribeInfo_t * const pxSubInfo = pxCallback->pxSubInfo;

            if( ( pxSubInfo != NULL ) &&
                prvMatchTopic( pxSubInfo,
                               pxPublishInfo->pTopicName,
                               pxPublishInfo->topicNameLength ) )
            {
                char * pcTaskName = pcTaskGetName( pxCallback->xTaskHandle );

                if( !pcTaskName )
                {
                    pcTaskName = "Unknown";
                }

                LogInfo( "Handling callback for task=%s, topic=\"%.*s\", filter=\"%.*s\".",
                         pcTaskName,
                         pxPublishInfo->topicNameLength, pxPublishInfo->pTopicName,
                         pxSubInfo->topicFilterLength, pxSubInfo->pTopicFilter );

                pxCallback->pxIncomingPublishCallback( pxCallback->pvIncomingPublishCallbackContext,
                                                       pxPublishInfo );
                xPublishHandled = true;
            }
        }

        ( void ) xUnlockSubCtx( pxCtx );
    }

    if( !xPublishHandled )
    {
        LogWarn( "Incoming publish with topic=\"%.*s\" does not match any callback functions.",
                 pxPublishInfo->topicNameLength, pxPublishInfo->pTopicName );
    }
}

/*-----------------------------------------------------------*/

static void prvSubscriptionManagerCtxFree( SubMgrCtx_t * pxSubMgrCtx )
{
    configASSERT( pxSubMgrCtx );

    if( pxSubMgrCtx->xMutex )
    {
        configASSERT_CONTINUE( MUTEX_IS_OWNED( pxSubMgrCtx->xMutex ) );
        vSemaphoreDelete( pxSubMgrCtx->xMutex );
    }
}

/*-----------------------------------------------------------*/

static void prvSubscriptionManagerCtxReset( SubMgrCtx_t * pxSubMgrCtx )
{
    configASSERT( pxSubMgrCtx );
    configASSERT_CONTINUE( MUTEX_IS_OWNED( pxSubMgrCtx->xMutex ) );

    pxSubMgrCtx->uxSubscriptionCount = 0;
    pxSubMgrCtx->uxCallbackCount = 0;

    for( size_t uxIdx = 0; uxIdx < MQTT_AGENT_MAX_SUBSCRIPTIONS; uxIdx++ )
    {
        pxSubMgrCtx->pxSubAckStatus[ uxIdx ] = MQTTSubAckFailure;
        pxSubMgrCtx->pulSubCbCount[ uxIdx ] = 0;

        pxSubMgrCtx->pxSubscriptions[ uxIdx ].pTopicFilter = NULL;
        pxSubMgrCtx->pxSubscriptions[ uxIdx ].qos = 0;
        pxSubMgrCtx->pxSubscriptions[ uxIdx ].topicFilterLength = 0;
    }

    for( size_t uxIdx = 0; uxIdx < MQTT_AGENT_MAX_CALLBACKS; uxIdx++ )
    {
        pxSubMgrCtx->pxCallbacks[ uxIdx ].pvIncomingPublishCallbackContext = NULL;
        pxSubMgrCtx->pxCallbacks[ uxIdx ].pxIncomingPublishCallback = NULL;
        pxSubMgrCtx->pxCallbacks[ uxIdx ].pxSubInfo = NULL;
        pxSubMgrCtx->pxCallbacks[ uxIdx ].xTaskHandle = NULL;
    }

    pxSubMgrCtx->xInitialSubscribeArgs.numSubscriptions = 0;
    pxSubMgrCtx->xInitialSubscribeArgs.pSubscribeInfo = NULL;
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvSubscriptionManagerCtxInit( SubMgrCtx_t * pxSubMgrCtx )
{
    MQTTStatus_t xStatus = MQTTSuccess;

    configASSERT( pxSubMgrCtx );

    pxSubMgrCtx->xMutex = xSemaphoreCreateMutex();

    if( pxSubMgrCtx->xMutex )
    {
        LogDebug( "Creating MqttAgent Mutex." );
        ( void ) xLockSubCtx( pxSubMgrCtx );
        prvSubscriptionManagerCtxReset( pxSubMgrCtx );
    }
    else
    {
        xStatus = MQTTNoMemory;
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

static void prvFreeAgentTaskCtx( MQTTAgentTaskCtx_t * pxCtx )
{
    if( pxCtx )
    {
        if( pxCtx->xAgentMessageCtx.xQueue != NULL )
        {
            vQueueDelete( pxCtx->xAgentMessageCtx.xQueue );
        }

        if( pxCtx->xConnectInfo.pClientIdentifier != NULL )
        {
            vPortFree( ( void * ) pxCtx->xConnectInfo.pClientIdentifier );
        }

        if( pxCtx->pcMqttEndpoint != NULL )
        {
            vPortFree( pxCtx->pcMqttEndpoint );
        }

        prvSubscriptionManagerCtxFree( &( pxCtx->xSubMgrCtx ) );

        vPortFree( ( void * ) pxCtx );
    }
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvConfigureAgentTaskCtx( MQTTAgentTaskCtx_t * pxCtx,
                                              NetworkContext_t * pxNetworkContext,
                                              uint8_t * pucNetworkBuffer,
                                              size_t uxNetworkBufferLen )
{
    BaseType_t xSuccess = pdTRUE;
    MQTTStatus_t xStatus = MQTTSuccess;
    size_t uxTempSize = 0;

    if( pxCtx == NULL )
    {
        xStatus = MQTTBadParameter;
        LogError( "Invalid pxCtx parameter." );
    }
    else if( pxNetworkContext == NULL )
    {
        xStatus = MQTTBadParameter;
        LogError( "Invalid pxNetworkContext parameter." );
    }
    else if( pucNetworkBuffer == NULL )
    {
        xStatus = MQTTBadParameter;
        LogError( "Invalid pucNetworkBuffer parameter." );
    }
    else
    {
        /* Zero Initialize */
        memset( pxCtx, 0, sizeof( MQTTAgentTaskCtx_t ) );
    }

    if( xStatus == MQTTSuccess )
    {
        /* Setup network buffer */
        pxCtx->xNetworkFixedBuffer.pBuffer = pucNetworkBuffer;
        pxCtx->xNetworkFixedBuffer.size = uxNetworkBufferLen;

        /* Setup transport interface */
        pxCtx->xTransport.pNetworkContext = pxNetworkContext;
        pxCtx->xTransport.send = mbedtls_transport_send;
        pxCtx->xTransport.recv = mbedtls_transport_recv;

        /* MQTTConnectInfo_t */
        /* Always start the initial connection with a clean session */
        pxCtx->xConnectInfo.cleanSession = true;
        pxCtx->xConnectInfo.keepAliveSeconds = KEEP_ALIVE_INTERVAL_S;
        pxCtx->xConnectInfo.pUserName = AWS_IOT_METRICS_STRING;
        pxCtx->xConnectInfo.userNameLength = AWS_IOT_METRICS_STRING_LENGTH;
        pxCtx->xConnectInfo.pPassword = NULL;
        pxCtx->xConnectInfo.passwordLength = 0U;

        pxCtx->xConnectInfo.pClientIdentifier = KVStore_getStringHeap( CS_CORE_THING_NAME, &uxTempSize );

        if( ( pxCtx->xConnectInfo.pClientIdentifier != NULL ) &&
            ( uxTempSize > 0 ) &&
            ( uxTempSize <= UINT16_MAX ) )
        {
            pxCtx->xConnectInfo.clientIdentifierLength = ( uint16_t ) uxTempSize;
        }
        else
        {
            LogError( "Invalid client identifier read from KVStore." );
            xStatus = MQTTNoMemory;
        }
    }

    if( xStatus == MQTTSuccess )
    {
        pxCtx->xAgentMessageCtx.xQueue = xQueueCreate( MQTT_AGENT_COMMAND_QUEUE_LENGTH,
                                                       sizeof( MQTTAgentCommand_t * ) );

        if( pxCtx->xAgentMessageCtx.xQueue == NULL )
        {
            xStatus = MQTTNoMemory;
            LogError( "Failed to allocate MQTT Agent message queue." );
        }

        pxCtx->xAgentMessageCtx.xAgentTaskHandle = xTaskGetCurrentTaskHandle();
    }

    if( xStatus == MQTTSuccess )
    {
        /* Setup message interface */
        pxCtx->xMessageInterface.pMsgCtx = &( pxCtx->xAgentMessageCtx );
        pxCtx->xMessageInterface.send = prvAgentMessageSend;
        pxCtx->xMessageInterface.recv = prvAgentMessageReceive;
        pxCtx->xMessageInterface.getCommand = Agent_GetCommand;
        pxCtx->xMessageInterface.releaseCommand = Agent_ReleaseCommand;
    }

    if( xStatus == MQTTSuccess )
    {
        pxCtx->pcMqttEndpoint = KVStore_getStringHeap( CS_CORE_MQTT_ENDPOINT,
                                                       &( pxCtx->uxMqttEndpointLen ) );

        if( ( pxCtx->uxMqttEndpointLen == 0 ) ||
            ( pxCtx->pcMqttEndpoint == NULL ) )
        {
            LogError( "Invalid mqtt endpoint read from KVStore." );
            xStatus = MQTTNoMemory;
        }
    }

    if( xStatus == MQTTSuccess )
    {
        pxCtx->ulMqttPort = KVStore_getUInt32( CS_CORE_MQTT_PORT, &( xSuccess ) );

        if( ( pxCtx->ulMqttPort == 0 ) ||
            ( xSuccess == pdFALSE ) )
        {
            LogError( "Invalid mqtt port number read from KVStore." );
            xStatus = MQTTNoMemory;
        }
    }

    if( xStatus == MQTTSuccess )
    {
        xStatus = prvSubscriptionManagerCtxInit( &( pxCtx->xSubMgrCtx ) );

        if( xStatus != MQTTSuccess )
        {
            LogError( "Failed to initialize Subscription Manager Context." );
        }
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

MQTTAgentHandle_t xGetMqttAgentHandle( void )
{
    return xDefaultInstanceHandle;
}

/*-----------------------------------------------------------*/

void vMQTTAgentTask( void * pvParameters )
{
    MQTTStatus_t xMQTTStatus = MQTTSuccess;
    TlsTransportStatus_t xTlsStatus = TLS_TRANSPORT_CONNECT_FAILURE;
    BaseType_t xExitFlag = pdFALSE;

    MQTTAgentTaskCtx_t * pxCtx = NULL;
    uint8_t * pucNetworkBuffer = NULL;
    NetworkContext_t * pxNetworkContext = NULL;
    uint16_t usNextRetryBackOff = 0U;

    PkiObject_t xPrivateKey = xPkiObjectFromLabel( TLS_KEY_PRV_LABEL );
    PkiObject_t xClientCertificate = xPkiObjectFromLabel( TLS_CERT_LABEL );
    PkiObject_t pxRootCaChain[ 1 ] = { xPkiObjectFromLabel( TLS_ROOT_CA_CERT_LABEL ) };

    ( void ) pvParameters;

    /* Miscellaneous initialization. */
    ulGlobalEntryTimeMs = prvGetTimeMs();

    /* Memory Allocation */
    pucNetworkBuffer = ( uint8_t * ) pvPortMalloc( MQTT_AGENT_NETWORK_BUFFER_SIZE );

    if( pucNetworkBuffer == NULL )
    {
        LogError( "Failed to allocate %d bytes for pucNetworkBuffer.", MQTT_AGENT_NETWORK_BUFFER_SIZE );
        xMQTTStatus = MQTTNoMemory;
    }

    if( xMQTTStatus == MQTTSuccess )
    {
        pxNetworkContext = mbedtls_transport_allocate();

        if( pxNetworkContext == NULL )
        {
            LogError( "Failed to allocate an mbedtls transport context." );
            xMQTTStatus = MQTTNoMemory;
        }
    }

    if( xMQTTStatus == MQTTSuccess )
    {
        xTlsStatus = mbedtls_transport_configure( pxNetworkContext,
                                                  pcAlpnProtocols,
                                                  &xPrivateKey,
                                                  &xClientCertificate,
                                                  pxRootCaChain,
                                                  1 );

        if( xTlsStatus != TLS_TRANSPORT_SUCCESS )
        {
            LogError( "Failed to configure mbedtls transport." );
            xMQTTStatus = MQTTBadParameter;
        }
    }

    if( xMQTTStatus == MQTTSuccess )
    {
        pxCtx = pvPortMalloc( sizeof( MQTTAgentTaskCtx_t ) );

        if( pxCtx != NULL )
        {
            xMQTTStatus = prvConfigureAgentTaskCtx( pxCtx, pxNetworkContext,
                                                    pucNetworkBuffer,
                                                    MQTT_AGENT_NETWORK_BUFFER_SIZE );
        }
        else
        {
            LogError( "Failed to allocate %d bytes for MQTTAgentTaskCtx_t.", sizeof( MQTTAgentTaskCtx_t ) );
            xMQTTStatus = MQTTNoMemory;
        }
    }

    if( xMQTTStatus == MQTTSuccess )
    {
        Agent_InitializePool();
    }

    if( xMQTTStatus == MQTTSuccess )
    {
        /* Initialize the MQTT context with the buffer and transport interface. */
        xMQTTStatus = MQTTAgent_Init( &( pxCtx->xAgentContext ),
                                      &( pxCtx->xMessageInterface ),
                                      &( pxCtx->xNetworkFixedBuffer ),
                                      &( pxCtx->xTransport ),
                                      prvGetTimeMs,
                                      prvIncomingPublishCallback,
                                      ( void * ) &( pxCtx->xSubMgrCtx ) );

        if( xMQTTStatus != MQTTSuccess )
        {
            LogError( "MQTTAgent_Init failed." );
        }
        else
        {
            ( void ) xEventGroupSetBits( xSystemEvents, EVT_MASK_MQTT_INIT );
            xDefaultInstanceHandle = &( pxCtx->xAgentContext );
        }
    }

    if( xMQTTStatus == MQTTSuccess )
    {
        xTlsStatus = mbedtls_transport_setrecvcallback( pxNetworkContext,
                                                        prvSocketRecvReadyCallback,
                                                        &( pxCtx->xAgentMessageCtx ) );

        if( xTlsStatus != TLS_TRANSPORT_SUCCESS )
        {
            LogError( "Failed to configure socket recv ready callback." );
            xMQTTStatus = MQTTBadParameter;
        }
    }

    if( xMQTTStatus != MQTTSuccess )
    {
        xExitFlag = pdTRUE;
    }

    /* Outer Reconnect loop */
    while( xExitFlag != pdTRUE )
    {
        BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;
        BackoffAlgorithmContext_t xReconnectParams = { 0 };

        /* Initialize backoff algorithm with jitter */
        BackoffAlgorithm_InitializeParams( &xReconnectParams,
                                           RETRY_BACKOFF_BASE,
                                           RETRY_MAX_BACKOFF_DELAY,
                                           BACKOFF_ALGORITHM_RETRY_FOREVER );

        xTlsStatus = TLS_TRANSPORT_UNKNOWN_ERROR;

        /* Connect a socket to the broker with retries */
        while( xTlsStatus != TLS_TRANSPORT_SUCCESS &&
               xBackoffAlgStatus == BackoffAlgorithmSuccess )
        {
            /* Block until the network interface is connected */
            ( void ) xEventGroupWaitBits( xSystemEvents,
                                          EVT_MASK_NET_CONNECTED,
                                          0x00,
                                          pdTRUE,
                                          portMAX_DELAY );

            LogInfo( "Attempting a TLS connection to %s:%d.",
                     pxCtx->pcMqttEndpoint, pxCtx->ulMqttPort );

            xTlsStatus = mbedtls_transport_connect( pxNetworkContext,
                                                    pxCtx->pcMqttEndpoint,
                                                    ( uint16_t ) pxCtx->ulMqttPort,
                                                    0, 0 );

            if( xTlsStatus != TLS_TRANSPORT_SUCCESS )
            {
                /* Get back-off value (in seconds) for the next connection retry. */
                xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &xReconnectParams,
                                                                     uxRand(),
                                                                     &usNextRetryBackOff );

                if( xBackoffAlgStatus == BackoffAlgorithmSuccess )
                {
                    LogWarn( "Connecting to the mqtt broker failed. "
                             "Retrying connection in %lu ms.",
                             RETRY_BACKOFF_MULTIPLIER * usNextRetryBackOff );
                    vTaskDelay( pdMS_TO_TICKS( RETRY_BACKOFF_MULTIPLIER * usNextRetryBackOff ) );
                }
                else
                {
                    LogError( "Connection to the broker failed, all attempts exhausted." );
                    xExitFlag = pdTRUE;
                }
            }
        }

        if( xTlsStatus == TLS_TRANSPORT_SUCCESS )
        {
            bool xSessionPresent = false;

            configASSERT_CONTINUE( MUTEX_IS_OWNED( pxCtx->xSubMgrCtx.xMutex ) );

            ( void ) MQTTAgent_CancelAll( &( pxCtx->xAgentContext ) );

            xMQTTStatus = MQTT_Connect( &( pxCtx->xAgentContext.mqttContext ),
                                        &( pxCtx->xConnectInfo ),
                                        NULL,
                                        CONNACK_RECV_TIMEOUT_MS,
                                        &xSessionPresent );

            configASSERT_CONTINUE( MUTEX_IS_OWNED( pxCtx->xSubMgrCtx.xMutex ) );

            /* Resume a session if desired. */
            if( ( xMQTTStatus == MQTTSuccess ) &&
                ( pxCtx->xConnectInfo.cleanSession == false ) )
            {
                configASSERT_CONTINUE( MUTEX_IS_OWNED( pxCtx->xSubMgrCtx.xMutex ) );
                LogInfo( "Resuming persistent MQTT Session." );

                xMQTTStatus = MQTTAgent_ResumeSession( &( pxCtx->xAgentContext ), xSessionPresent );

                /* Re-subscribe to all the previously subscribed topics if there is no existing session. */
                if( ( xMQTTStatus == MQTTSuccess ) &&
                    ( xSessionPresent == false ) )
                {
                    xMQTTStatus = prvHandleResubscribe( &( pxCtx->xAgentContext ),
                                                        &( pxCtx->xSubMgrCtx ) );
                }
            }
            else if( xMQTTStatus == MQTTSuccess )
            {
                configASSERT_CONTINUE( MUTEX_IS_OWNED( pxCtx->xSubMgrCtx.xMutex ) );

                LogInfo( "Starting a clean MQTT Session." );

                prvSubscriptionManagerCtxReset( &( pxCtx->xSubMgrCtx ) );

                ( void ) xUnlockSubCtx( &( pxCtx->xSubMgrCtx ) );
            }
            else
            {
                LogError( "Failed to connect to mqtt broker." );
            }

            /* Further reconnects will include a session resume operation */
            if( xMQTTStatus == MQTTSuccess )
            {
                pxCtx->xConnectInfo.cleanSession = false;
            }
        }
        else
        {
            xMQTTStatus = MQTTKeepAliveTimeout;
        }

        if( xMQTTStatus == MQTTSuccess )
        {
            ( void ) xEventGroupSetBits( xSystemEvents, EVT_MASK_MQTT_CONNECTED );

            /* Reset backoff timer */
            BackoffAlgorithm_InitializeParams( &xReconnectParams,
                                               RETRY_BACKOFF_BASE,
                                               RETRY_MAX_BACKOFF_DELAY,
                                               BACKOFF_ALGORITHM_RETRY_FOREVER );

            /* MQTTAgent_CommandLoop() is effectively the agent implementation.  It
             * will manage the MQTT protocol until such time that an error occurs,
             * which could be a disconnect.  If an error occurs the MQTT context on
             * which the error happened is returned so there can be an attempt to
             * clean up and reconnect however the application writer prefers. */
            xMQTTStatus = MQTTAgent_CommandLoop( &( pxCtx->xAgentContext ) );

            LogDebug( "MQTTAgent_CommandLoop returned with status: %s.",
                      MQTT_Status_strerror( xMQTTStatus ) );
        }

        ( void ) MQTTAgent_CancelAll( &( pxCtx->xAgentContext ) );

        mbedtls_transport_disconnect( pxNetworkContext );

        ( void ) xEventGroupClearBits( xSystemEvents, EVT_MASK_MQTT_CONNECTED );

        /* Wait for any subscription related calls to complete */
        if( !MUTEX_IS_OWNED( pxCtx->xSubMgrCtx.xMutex ) )
        {
            ( void ) xLockSubCtx( &( pxCtx->xSubMgrCtx ) );
        }

        /* Reset subscription status */
        memset( pxCtx->xSubMgrCtx.pxSubAckStatus,
                MQTTSubAckFailure,
                sizeof( pxCtx->xSubMgrCtx.pxSubAckStatus ) );

        if( !xExitFlag )
        {
            /* Get back-off value (in seconds) for the next connection retry. */
            xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &xReconnectParams,
                                                                 uxRand(),
                                                                 &usNextRetryBackOff );

            if( xBackoffAlgStatus == BackoffAlgorithmSuccess )
            {
                LogWarn( "Disconnected from the MQTT Broker. Retrying in %lu ms.",
                         RETRY_BACKOFF_MULTIPLIER * usNextRetryBackOff );

                vTaskDelay( pdMS_TO_TICKS( RETRY_BACKOFF_MULTIPLIER * usNextRetryBackOff ) );
            }
            else
            {
                LogError( "Reconnect limit reached. Will exit..." );
                xExitFlag = pdTRUE;
            }
        }
    }

    if( pxCtx != NULL )
    {
        prvFreeAgentTaskCtx( pxCtx );
        pxCtx = NULL;
    }

    if( pxNetworkContext != NULL )
    {
        mbedtls_transport_disconnect( pxNetworkContext );
        mbedtls_transport_free( pxNetworkContext );
        pxNetworkContext = NULL;
    }

    ( void ) xEventGroupClearBits( xSystemEvents, EVT_MASK_MQTT_INIT | EVT_MASK_MQTT_CONNECTED );

    LogError( "Terminating MqttAgentTask." );

    vTaskDelete( NULL );
}

/*-----------------------------------------------------------*/

static uint32_t prvGetTimeMs( void )
{
    uint32_t ulTimeMs = 0UL;

    /* Determine the elapsed time in the application */
    ulTimeMs = ( uint32_t ) ( xTaskGetTickCount() * portTICK_PERIOD_MS ) - ulGlobalEntryTimeMs;

    return ulTimeMs;
}

/*-----------------------------------------------------------*/

static inline MQTTQoS_t prvGetNewQoS( MQTTQoS_t xCurrentQoS,
                                      MQTTQoS_t xRequestedQoS )
{
    MQTTQoS_t xNewQoS;

    if( xCurrentQoS == xRequestedQoS )
    {
        xNewQoS = xCurrentQoS;
    }
    /* Otherwise, upgrade to QoS1 */
    else
    {
        xNewQoS = MQTTQoS1;
    }

    return xNewQoS;
}

/*-----------------------------------------------------------*/

static inline bool prvValidateQoS( MQTTQoS_t xQoS )
{
    return( xQoS == MQTTQoS0 || xQoS == MQTTQoS1 || xQoS == MQTTQoS2 );
}


/*-----------------------------------------------------------*/

static void prvSubscribeRqCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                    MQTTAgentReturnInfo_t * pxReturnInfo )
{
    TaskHandle_t xTaskHandle = ( TaskHandle_t ) pxCommandContext;

    configASSERT( pxReturnInfo );

    if( xTaskHandle != NULL )
    {
        uint32_t ulNotifyValue = ( pxReturnInfo->returnCode & 0xFFFFFF );

        if( pxReturnInfo->pSubackCodes )
        {
            ulNotifyValue += ( pxReturnInfo->pSubackCodes[ 0 ] << 24 );
        }

        ( void ) xTaskNotifyIndexed( xTaskHandle,
                                     MQTT_AGENT_NOTIFY_IDX,
                                     ulNotifyValue,
                                     eSetValueWithOverwrite );
    }
}

/*-----------------------------------------------------------*/

static inline MQTTStatus_t prvSendSubRequest( MQTTAgentContext_t * pxAgentCtx,
                                              MQTTSubscribeInfo_t * pxSubInfo,
                                              MQTTSubAckStatus_t * pxSubAckStatus,
                                              TickType_t xTimeout )
{
    MQTTStatus_t xStatus;

    MQTTAgentSubscribeArgs_t xSubscribeArgs =
    {
        .numSubscriptions = 1,
        .pSubscribeInfo   = pxSubInfo,
    };

    MQTTAgentCommandInfo_t xCommandInfo =
    {
        .blockTimeMs                 = xTimeout,
        .cmdCompleteCallback         = prvSubscribeRqCallback,
        .pCmdCompleteCallbackContext = ( void * ) ( xTaskGetCurrentTaskHandle() ),
    };

    configASSERT( pxAgentCtx );
    configASSERT( pxSubInfo );
    configASSERT( pxSubAckStatus );

    ( void ) xTaskNotifyStateClearIndexed( NULL, MQTT_AGENT_NOTIFY_IDX );

    xStatus = MQTTAgent_Subscribe( pxAgentCtx, &xSubscribeArgs, &xCommandInfo );

    LogInfo( "MQTT Subscribe, filter=\"%.*s\"", pxSubInfo->topicFilterLength, pxSubInfo->pTopicFilter );

    if( xStatus == MQTTSuccess )
    {
        uint32_t ulNotifyValue = 0;

        if( xTaskNotifyWaitIndexed( MQTT_AGENT_NOTIFY_IDX,
                                    0x0,
                                    0xFFFFFFFF,
                                    &ulNotifyValue,
                                    xTimeout ) )
        {
            *pxSubAckStatus = ( ulNotifyValue >> 24 );
            xStatus = ( ulNotifyValue & 0x00FFFFFF );
        }
        else
        {
            xStatus = MQTTKeepAliveTimeout;
        }
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

MQTTStatus_t MqttAgent_SubscribeSync( MQTTAgentHandle_t xHandle,
                                      const char * pcTopicFilter,
                                      MQTTQoS_t xRequestedQoS,
                                      IncomingPubCallback_t pxCallback,
                                      void * pvCallbackCtx )
{
    MQTTStatus_t xStatus = MQTTSuccess;
    size_t xTopicFilterLen = 0;
    MQTTAgentTaskCtx_t * pxTaskCtx = ( MQTTAgentTaskCtx_t * ) xHandle;
    SubMgrCtx_t * pxCtx = &( pxTaskCtx->xSubMgrCtx );

    if( ( xHandle == NULL ) ||
        ( pcTopicFilter == NULL ) ||
        ( pxCallback == NULL ) ||
        !prvValidateQoS( xRequestedQoS ) )
    {
        xStatus = MQTTBadParameter;
    }
    else /* pcTopicFilter != NULL */
    {
        xTopicFilterLen = strnlen( pcTopicFilter, UINT16_MAX );
    }

    if( ( xTopicFilterLen == 0 ) || ( xTopicFilterLen >= UINT16_MAX ) )
    {
        xStatus = MQTTBadParameter;
    }

    /* Acquire mutex */
    if( ( xStatus == MQTTSuccess ) &&
        xLockSubCtx( pxCtx ) )
    {
        size_t uxTargetSubIdx = MQTT_AGENT_MAX_SUBSCRIPTIONS;
        size_t uxTargetCbIdx = MQTT_AGENT_MAX_CALLBACKS;

        /* If no slot is found, return MQTTNoMemory */
        xStatus = MQTTNoMemory;

        for( size_t uxSubIdx = 0U; uxSubIdx < MQTT_AGENT_MAX_SUBSCRIPTIONS; uxSubIdx++ )
        {
            MQTTSubscribeInfo_t * const pxSubInfo = &( pxCtx->pxSubscriptions[ uxSubIdx ] );

            if( ( pxCtx->pxSubscriptions[ uxSubIdx ].pTopicFilter == NULL ) &&
                ( uxTargetSubIdx == MQTT_AGENT_MAX_SUBSCRIPTIONS ) )
            {
                /* Check that the current context is indeed empty */
                configASSERT( pxCtx->pxSubscriptions[ uxSubIdx ].topicFilterLength == 0 );

                uxTargetSubIdx = uxSubIdx;
                xStatus = MQTTSuccess;

                /* Reset SubAckStatus to trigger a subscribe op */
                pxCtx->pxSubAckStatus[ uxTargetSubIdx ] = MQTTSubAckFailure;
            }
            else if( strncmp( pxSubInfo->pTopicFilter, pcTopicFilter, pxSubInfo->topicFilterLength ) == 0 )
            {
                xRequestedQoS = prvGetNewQoS( pxSubInfo->qos, xRequestedQoS );
                xStatus = MQTTSuccess;
                uxTargetSubIdx = uxSubIdx;

                /* If QoS differs, trigger a subscribe op */
                if( pxSubInfo->qos != xRequestedQoS )
                {
                    pxCtx->pxSubAckStatus[ uxTargetSubIdx ] = MQTTSubAckFailure;
                }

                break;
            }
            else
            {
                /* Empty */
            }
        }

        /* Add Callback to list */
        if( xStatus == MQTTSuccess )
        {
            /* If no slot is found, return MQTTNoMemory */
            xStatus = MQTTNoMemory;

            /* Find matching or empty callback context */
            for( size_t uxCbIdx = 0U; uxCbIdx < MQTT_AGENT_MAX_CALLBACKS; uxCbIdx++ )
            {
                if( ( uxTargetCbIdx == MQTT_AGENT_MAX_CALLBACKS ) &&
                    ( pxCtx->pxCallbacks[ uxCbIdx ].pxSubInfo == NULL ) )
                {
                    uxTargetCbIdx = uxCbIdx;
                    xStatus = MQTTSuccess;
                }
                else if( prvMatchCbCtx( &( pxCtx->pxCallbacks[ uxCbIdx ] ),
                                        &( pxCtx->pxSubscriptions[ uxTargetSubIdx ] ),
                                        pxCallback,
                                        pvCallbackCtx ) )
                {
                    uxTargetCbIdx = uxCbIdx;
                    xStatus = MQTTSuccess;
                    break;
                }
            }
        }

        /*
         * Populate the subscription entry (by copying topic filter to heap)
         */
        if( ( xStatus == MQTTSuccess ) &&
            ( pxCtx->pxSubAckStatus[ uxTargetSubIdx ] == MQTTSubAckFailure ) )
        {
            if( pxCtx->pxSubscriptions[ uxTargetSubIdx ].pTopicFilter == NULL )
            {
                char * pcDupTopicFilter = pvPortMalloc( xTopicFilterLen + 1 );

                if( pcDupTopicFilter == NULL )
                {
                    xStatus = MQTTNoMemory;
                }
                else
                {
                    ( void ) strncpy( pcDupTopicFilter, pcTopicFilter, xTopicFilterLen + 1 );

                    /* Ensure null terminated */
                    pcDupTopicFilter[ xTopicFilterLen ] = '\00';

                    pxCtx->pxSubscriptions[ uxTargetSubIdx ].pTopicFilter = pcDupTopicFilter;
                    pxCtx->pxSubscriptions[ uxTargetSubIdx ].topicFilterLength = ( uint16_t ) xTopicFilterLen;

                    pxCtx->uxSubscriptionCount++;
                }
            }

            pxCtx->pxSubscriptions[ uxTargetSubIdx ].qos = xRequestedQoS;
        }

        /*
         * Populate the callback entry
         */
        if( ( xStatus == MQTTSuccess ) &&
            ( pxCtx->pxCallbacks[ uxTargetCbIdx ].pxSubInfo == NULL ) )
        {
            pxCtx->pxCallbacks[ uxTargetCbIdx ].pxSubInfo = &( pxCtx->pxSubscriptions[ uxTargetSubIdx ] );
            pxCtx->pxCallbacks[ uxTargetCbIdx ].xTaskHandle = xTaskGetCurrentTaskHandle();
            pxCtx->pxCallbacks[ uxTargetCbIdx ].pxIncomingPublishCallback = pxCallback;
            pxCtx->pxCallbacks[ uxTargetCbIdx ].pvIncomingPublishCallbackContext = pvCallbackCtx;

            /* Increment subscription reference count. */
            pxCtx->pulSubCbCount[ uxTargetSubIdx ]++;

            pxCtx->uxCallbackCount++;

            LogInfo( "Callback registered with filter=\"%.*s\".", xTopicFilterLen, pcTopicFilter );
        }

        ( void ) xUnlockSubCtx( pxCtx );

        if( ( xStatus == MQTTSuccess ) &&
            ( pxCtx->pxSubAckStatus[ uxTargetSubIdx ] == MQTTSubAckFailure ) )
        {
            xStatus = prvSendSubRequest( &( pxTaskCtx->xAgentContext ),
                                         &( pxCtx->pxSubscriptions[ uxTargetSubIdx ] ),
                                         &( pxCtx->pxSubAckStatus[ uxTargetSubIdx ] ),
                                         portMAX_DELAY );
        }
    }
    else
    {
        xStatus = MQTTIllegalState;
        LogError( "Failed to acquire MQTTAgent mutex." );
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

static void prvAgentRequestCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                     MQTTAgentReturnInfo_t * pxReturnInfo )
{
    TaskHandle_t xTaskHandle = ( TaskHandle_t ) pxCommandContext;

    configASSERT( pxReturnInfo );

    if( xTaskHandle != NULL )
    {
        uint32_t ulNotifyValue = pxReturnInfo->returnCode;
        ( void ) xTaskNotifyIndexed( xTaskHandle,
                                     MQTT_AGENT_NOTIFY_IDX,
                                     ulNotifyValue,
                                     eSetValueWithOverwrite );
    }
}

/*-----------------------------------------------------------*/

static inline MQTTStatus_t prvSendUnsubRequest( MQTTAgentContext_t * pxAgentCtx,
                                                const char * pcTopicFilter,
                                                size_t xTopicFilterLen,
                                                MQTTQoS_t xQos,
                                                TickType_t xTimeout )
{
    MQTTStatus_t xStatus = MQTTSuccess;

    MQTTSubscribeInfo_t xSubInfo =
    {
        .pTopicFilter      = pcTopicFilter,
        .topicFilterLength = ( uint16_t ) xTopicFilterLen,
        .qos               = xQos,
    };

    MQTTAgentSubscribeArgs_t xSubscribeArgs =
    {
        .numSubscriptions = 1,
        .pSubscribeInfo   = &xSubInfo,
    };

    MQTTAgentCommandInfo_t xCommandInfo =
    {
        .blockTimeMs                 = xTimeout,
        .cmdCompleteCallback         = prvAgentRequestCallback,
        .pCmdCompleteCallbackContext = ( void * ) xTaskGetCurrentTaskHandle(),
    };

    ( void ) xTaskNotifyStateClearIndexed( NULL, MQTT_AGENT_NOTIFY_IDX );

    xStatus = MQTTAgent_Unsubscribe( pxAgentCtx, &xSubscribeArgs, &xCommandInfo );

    if( xStatus == MQTTSuccess )
    {
        uint32_t ulNotifyValue = 0;

        LogInfo( "MQTT Unsubscribe: \"%.*s\"", xTopicFilterLen, pcTopicFilter );

        if( xTaskNotifyWaitIndexed( MQTT_AGENT_NOTIFY_IDX,
                                    0x0,
                                    0xFFFFFFFF,
                                    &ulNotifyValue,
                                    xTimeout ) )
        {
            xStatus = ulNotifyValue;
        }
        else
        {
            xStatus = MQTTKeepAliveTimeout;
        }
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

MQTTStatus_t MqttAgent_UnSubscribeSync( MQTTAgentHandle_t xHandle,
                                        const char * pcTopicFilter,
                                        IncomingPubCallback_t pxCallback,
                                        void * pvCallbackCtx )
{
    MQTTStatus_t xStatus = MQTTSuccess;
    size_t xTopicFilterLen = 0;
    MQTTAgentTaskCtx_t * pxTaskCtx = ( MQTTAgentTaskCtx_t * ) xHandle;
    SubMgrCtx_t * pxCtx = &( pxTaskCtx->xSubMgrCtx );
    uint32_t ulCallbackCount = 0;

    if( ( xHandle == NULL ) ||
        ( pcTopicFilter == NULL ) ||
        ( pxCallback == NULL ) )
    {
        xStatus = MQTTBadParameter;
    }
    else /* pcTopicFilter != NULL */
    {
        xTopicFilterLen = strnlen( pcTopicFilter, UINT16_MAX );
    }

    if( ( xTopicFilterLen == 0 ) || ( xTopicFilterLen >= UINT16_MAX ) )
    {
        xStatus = MQTTBadParameter;
    }

    if( xStatus == MQTTSuccess )
    {
        xStatus = MQTTNoDataAvailable;

        /* Acquire mutex */
        if( xLockSubCtx( pxCtx ) )
        {
            /* Find matching subscription context */
            for( size_t uxIdx = 0U; uxIdx < MQTT_AGENT_MAX_SUBSCRIPTIONS; uxIdx++ )
            {
                if( ( xTopicFilterLen == pxCtx->pxSubscriptions[ uxIdx ].topicFilterLength ) &&
                    ( strncmp( pxCtx->pxSubscriptions[ uxIdx ].pTopicFilter,
                               pcTopicFilter,
                               pxCtx->pxSubscriptions[ uxIdx ].topicFilterLength ) == 0 ) )
                {
                    ulCallbackCount = pxCtx->pulSubCbCount[ uxIdx ];

                    if( ulCallbackCount > 0 )
                    {
                        pxCtx->pulSubCbCount[ uxIdx ] = ulCallbackCount - 1;
                    }

                    xStatus = MQTTSuccess;
                    break;
                }
            }

            ( void ) xUnlockSubCtx( pxCtx );
        }
        else
        {
            xStatus = MQTTIllegalState;
            LogError( "Failed to acquire MQTTAgent mutex." );
        }

        /* Send unsubscribe request if only one callback is left for this subscription */
        if( ( xStatus == MQTTSuccess ) &&
            ( ulCallbackCount == 1 ) )
        {
            /* TODO: Use a reasonable timeout value here */
            xStatus = prvSendUnsubRequest( &( pxTaskCtx->xAgentContext ),
                                           pcTopicFilter,
                                           xTopicFilterLen,
                                           MQTTQoS1,
                                           portMAX_DELAY );
        }

        /* Acquire mutex */
        if( xLockSubCtx( pxCtx ) )
        {
            MQTTSubscribeInfo_t * pxSubInfo = NULL;
            size_t uxSubInfoIdx = MQTT_AGENT_MAX_SUBSCRIPTIONS;

            /* Find matching subscription context again */
            for( size_t uxIdx = 0U; uxIdx < MQTT_AGENT_MAX_SUBSCRIPTIONS; uxIdx++ )
            {
                if( ( pxCtx->pulSubCbCount[ uxIdx ] == 0 ) &&
                    ( xTopicFilterLen == pxCtx->pxSubscriptions[ uxIdx ].topicFilterLength ) &&
                    ( strncmp( pxCtx->pxSubscriptions[ uxIdx ].pTopicFilter,
                               pcTopicFilter,
                               pxCtx->pxSubscriptions[ uxIdx ].topicFilterLength ) == 0 ) )
                {
                    uxSubInfoIdx = uxIdx;
                    pxSubInfo = &( pxCtx->pxSubscriptions[ uxIdx ] );
                    xStatus = MQTTSuccess;
                    break;
                }
            }

            if( xStatus == MQTTSuccess )
            {
                xStatus = MQTTNoDataAvailable;

                /* Find matching callback context, and remove it. */
                for( size_t uxIdx = 0U; uxIdx < MQTT_AGENT_MAX_CALLBACKS; uxIdx++ )
                {
                    SubCallbackElement_t * pxCbCtx = &( pxCtx->pxCallbacks[ uxIdx ] );

                    if( prvMatchCbCtx( pxCbCtx, pxSubInfo, pxCallback, pvCallbackCtx ) )
                    {
                        xStatus = MQTTSuccess;
                        pxCbCtx->pvIncomingPublishCallbackContext = NULL;
                        pxCbCtx->pxIncomingPublishCallback = NULL;
                        pxCbCtx->pxSubInfo = NULL;
                        pxCbCtx->xTaskHandle = NULL;

                        configASSERT( pxCtx->uxCallbackCount > 0 );

                        pxCtx->uxCallbackCount--;

                        LogInfo( "Callback de-registered, filter=\"%.*s\".", xTopicFilterLen, pcTopicFilter );

                        prvCompressCallbackList( pxCtx->pxCallbacks, &( pxCtx->uxCallbackCount ) );
                        break;
                    }
                }

                if( ( xStatus == MQTTSuccess ) &&
                    ( ulCallbackCount == 1 ) &&
                    ( pxCtx->pulSubCbCount[ uxSubInfoIdx ] == 0 ) )
                {
                    /* Free heap allocated topic filter */
                    vPortFree( ( void * ) pxSubInfo->pTopicFilter );

                    pxSubInfo->pTopicFilter = NULL;
                    pxSubInfo->topicFilterLength = 0;
                    pxSubInfo->qos = 0;
                    pxCtx->pxSubAckStatus[ uxSubInfoIdx ] = MQTTSubAckFailure;

                    configASSERT( pxCtx->uxSubscriptionCount > 0 );

                    if( pxCtx->uxSubscriptionCount > 0 )
                    {
                        pxCtx->uxSubscriptionCount--;
                    }
                }
            }

            ( void ) xUnlockSubCtx( pxCtx );
        }
        else
        {
            xStatus = MQTTIllegalState;
            LogError( "Failed to acquire MQTTAgent mutex." );
        }
    }

    return xStatus;
}
