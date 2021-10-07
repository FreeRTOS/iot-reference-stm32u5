/*
 * Lab-Project-coreMQTT-Agent 201215
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

#include "logging_levels.h"
#define LOG_LEVEL    LOG_DEBUG
#include "logging.h"

/* Standard includes. */
#include <string.h>
#include <stdio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "event_groups.h"

#include "kvstore.h"
#include "mqtt_metrics.h"

/* MQTT library includes. */
#include "core_mqtt.h"

/* MQTT agent include. */
#include "core_mqtt_agent.h"

/* MQTT Agent ports. */
#include "freertos_agent_message.h"
#include "freertos_command_pool.h"
#include "core_pkcs11_config.h"

/* Exponential backoff retry include. */
#include "backoff_algorithm.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

#include "mbedtls_transport.h"
#include "sys_evt.h"

/*-----------------------------------------------------------*/
extern TransportInterfaceExtended_t xLwipTransportInterface;

/**
 * @brief Timeout for receiving CONNACK after sending an MQTT CONNECT packet.
 * Defined in milliseconds.
 */
#define CONNACK_RECV_TIMEOUT_MS                      ( 2000U )

/**
 * @brief The maximum number of retries for network operation with server.
 */
#define RETRY_MAX_ATTEMPTS                           ( 500U )

/**
 * @brief The maximum back-off delay (in milliseconds) for retrying failed operation
 *  with server.
 */
#define RETRY_MAX_BACKOFF_DELAY_S                    ( 5U * 60U )

/**
 * @brief The base back-off delay (in milliseconds) to use for network operation retry
 * attempts.
 */
#define RETRY_BACKOFF_BASE_S                         ( 10U )

/**
 * @brief The maximum time interval in seconds which is allowed to elapse
 *  between two Control Packets.
 *
 *  It is the responsibility of the Client to ensure that the interval between
 *  Control Packets being sent does not exceed the this Keep Alive value. In the
 *  absence of sending any other Control Packets, the Client MUST send a
 *  PINGREQ Packet.
 */
#define KEEP_ALIVE_INTERVAL_S                       ( 1200U )

/**
 * @brief Socket send and receive timeouts to use.
 */
#define SEND_RECV_TIMEOUT_MS                        ( 2000U )

#define AGENT_READY_EVT_MASK                        ( 0b1U )

typedef struct
{
    /* OSI Layer 5 parameters */
    char * pcMqttEndpointAddress;
    uint32_t ulMqttEndpointLen;
    uint32_t ulMqttPort;

    /* OSI Layer 6 parameters */
    NetworkCredentials_t xNetworkCredentials;

    /* OSI Layer 7 parameters */
    MQTTConnectInfo_t xCInfo;
} MqttConnectCtx_t;

/*-----------------------------------------------------------*/

/**
 * @brief Initializes an MQTT context, including transport interface and
 * network buffer.
 *
 * @param[in] pxNetworkContext Pointer to the network context to connect with.
 *
 * @return `MQTTSuccess` if the initialization succeeds, else `MQTTBadParameter`.
 */
static MQTTStatus_t prvMQTTInit( NetworkContext_t * pxNetworkContext );

/**
 * @brief Sends an MQTT Connect packet over the already connected TCP socket.
 *
 * @param[in] xCleanSession If a clean session should be established.
 * @param[in] pxConnectCtx MQTT connect operation context pointer.
 *
 * @return `MQTTSuccess` if connection succeeds, else appropriate error code
 * from MQTT_Connect.
 */
static MQTTStatus_t prvMQTTConnect( bool xCleanSession,
                                    MqttConnectCtx_t * pxConnectCtx );

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
static MQTTStatus_t prvHandleResubscribe( void );

/**
 * @brief Passed into MQTTAgent_Subscribe() as the callback to execute when the
 * broker ACKs the SUBSCRIBE message. This callback implementation is used for
 * handling the completion of resubscribes. Any topic filter failed to resubscribe
 * will be removed from the subscription list.
 *
 * See https://freertos.org/mqtt/mqtt-agent-demo.html#example_mqtt_api_call
 *
 * @param[in] pxCommandContext Context of the initial command.
 * @param[in] pxReturnInfo The result of the command.
 */
static void prvSubscriptionCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                            MQTTAgentReturnInfo_t * pxReturnInfo );

/**
 * @brief The timer query function provided to the MQTT context.
 *
 * @return Time in milliseconds.
 */
static uint32_t prvGetTimeMs( void );


/*
 * Functions that start the tasks demonstrated by this project.
 */

extern void vStartOTACodeSigningDemo( configSTACK_DEPTH_TYPE uxStackSize,
                                      UBaseType_t uxPriority );
extern void vSuspendOTACodeSigningDemo( void );
extern void vResumeOTACodeSigningDemo( void );

extern void vStartDefenderDemo( configSTACK_DEPTH_TYPE uxStackSize,
                                UBaseType_t uxPriority );

extern void vStartShadowDemo( configSTACK_DEPTH_TYPE uxStackSize,
                              UBaseType_t uxPriority );
/*-----------------------------------------------------------*/

/**
 * @brief The network context used by the MQTT library transport interface.
 * See https://www.freertos.org/network-interface.html
 */
//static NetworkContext_t * pxNetworkContext = NULL;

/**
 * @brief Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the chances
 * of overflow for the 32 bit unsigned integer used for holding the timestamp.
 */
static uint32_t ulGlobalEntryTimeMs;

MQTTAgentContext_t xGlobalMqttAgentContext;

static uint8_t xNetworkBuffer[ MQTT_AGENT_NETWORK_BUFFER_SIZE ];

static MQTTAgentMessageContext_t xCommandQueue;

/**
 * @brief The global array of subscription elements.
 *
 * @note No thread safety is required to this array, since the updates the array
 * elements are done only from one task at a time. The subscription manager
 * implementation expects that the array of the subscription elements used for
 * storing subscriptions to be initialized to 0. As this is a global array, it
 * will be initialized to 0 by default.
 */
SubscriptionElement_t xGlobalSubscriptionList[ SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS ];

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
}

/*-----------------------------------------------------------*/

static void vInitNetCredentials( NetworkCredentials_t * pxNetworkCredentials )
{
    configASSERT( pxNetworkCredentials != NULL );

    /* ALPN protocols must be a NULL-terminated list of strings. Therefore,
     * the first entry will contain the actual ALPN protocol string while the
     * second entry must remain NULL. */
    static const char * pcAlpnProtocols[] = { AWS_IOT_MQTT_ALPN, NULL };

    pxNetworkCredentials->pAlpnProtos = pcAlpnProtocols;

    pxNetworkCredentials->xSkipCaVerify = KVStore_getBase( CS_TLS_VERIFY_CA, NULL );
    pxNetworkCredentials->xSkipSNI = KVStore_getBase( CS_TLS_VERIFY_SNI, NULL );

    /* Set the credentials for establishing a TLS connection. */
    pxNetworkCredentials->xRootCaCertForm = OBJ_FORM_PKCS11_LABEL;
    pxNetworkCredentials->pvRootCaCert = pkcs11_ROOT_CA_CERT_LABEL;
    pxNetworkCredentials->rootCaCertSize = sizeof( pkcs11_ROOT_CA_CERT_LABEL );

    pxNetworkCredentials->xClientCertForm = OBJ_FORM_PKCS11_LABEL;
    pxNetworkCredentials->pvClientCert = pkcs11_TLS_CERT_LABEL;
    pxNetworkCredentials->clientCertSize = sizeof( pkcs11_TLS_CERT_LABEL );

    pxNetworkCredentials->xPrivateKeyForm = OBJ_FORM_PKCS11_LABEL;
    pxNetworkCredentials->pvPrivateKey = pkcs11_TLS_KEY_PRV_LABEL;
    pxNetworkCredentials->privateKeySize = sizeof( pkcs11_TLS_KEY_PRV_LABEL );
}
/*-----------------------------------------------------------*/

static BaseType_t xInitializeMqttConnectCtx( MqttConnectCtx_t * pxCtx )
{

    /* OSI L5 / L6 parameters */

    /* Get endpoint address */
    pxCtx->ulMqttEndpointLen = KVStore_getSize( CS_CORE_MQTT_ENDPOINT );

    configASSERT( pxCtx->ulMqttEndpointLen > 0 );

    pxCtx->pcMqttEndpointAddress = ( char * ) pvPortMalloc( pxCtx->ulMqttEndpointLen );

    if( pxCtx->pcMqttEndpointAddress == NULL )
    {
        pxCtx->ulMqttEndpointLen = 0;
    }
    else
    {
        ( void ) KVStore_getString( CS_CORE_MQTT_ENDPOINT,
                                    pxCtx->pcMqttEndpointAddress,
                                    pxCtx->ulMqttEndpointLen );
    }

    /* Get port */
    pxCtx->ulMqttPort = KVStore_getUInt32( CS_CORE_MQTT_PORT, NULL );


    /* OSI L7 parameters */

    /* Zero initialize any unused fields */
    memset( &( pxCtx->xCInfo ), 0x00, sizeof( MQTTConnectInfo_t ) );

    pxCtx->xCInfo.cleanSession = true;

    /* ??? Set MQTT keep-alive period. It is the responsibility of the application
     * to ensure that the interval between Control Packets being sent does not
     * exceed the Keep Alive value. In the absence of sending any other Control
     * Packets, the Client MUST send a PINGREQ Packet.  This responsibility will
     * be moved inside the agent. ??? */
    pxCtx->xCInfo.keepAliveSeconds = KEEP_ALIVE_INTERVAL_S;

    /* Append metrics when connecting to the AWS IoT Core broker. */
    pxCtx->xCInfo.pUserName = AWS_IOT_METRICS_STRING;
    pxCtx->xCInfo.userNameLength = AWS_IOT_METRICS_STRING_LENGTH;

    /* Password authentication is not used. */
    pxCtx->xCInfo.pPassword = NULL;
    pxCtx->xCInfo.passwordLength = 0U;


    /* Store client identifier / thing name */
    pxCtx->xCInfo.clientIdentifierLength = KVStore_getSize( CS_CORE_THING_NAME );

    configASSERT( pxCtx->xCInfo.clientIdentifierLength > 0 );

    pxCtx->xCInfo.pClientIdentifier = ( char * ) pvPortMalloc( pxCtx->xCInfo.clientIdentifierLength );

    if( pxCtx->xCInfo.pClientIdentifier == NULL )
    {
        pxCtx->xCInfo.clientIdentifierLength = 0;
    }
    else
    {
        ( void ) KVStore_getString( CS_CORE_THING_NAME,
                                    pxCtx->xCInfo.pClientIdentifier,
                                    pxCtx->xCInfo.clientIdentifierLength );
    }

    vInitNetCredentials( &( pxCtx->xNetworkCredentials ) );

    return( pxCtx->pcMqttEndpointAddress != NULL );
}

/*-----------------------------------------------------------*/

static void vDeinitMqttConnectCtx(  MqttConnectCtx_t * pxCtx )
{
    if( pxCtx != NULL )
    {
        if( pxCtx->pcMqttEndpointAddress != NULL )
        {
            vPortFree( pxCtx->pcMqttEndpointAddress );
            pxCtx->pcMqttEndpointAddress = NULL;
            pxCtx->ulMqttEndpointLen = 0;
        }

        if( pxCtx->xCInfo.pClientIdentifier != NULL )
        {
            vPortFree( pxCtx->xCInfo.pClientIdentifier );
            pxCtx->xCInfo.pClientIdentifier = NULL;
            pxCtx->xCInfo.clientIdentifierLength = 0;
        }
    }
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvMQTTInit(  NetworkContext_t * pxNetworkContext )
{
    TransportInterface_t xTransport;
    MQTTStatus_t xReturn;
    MQTTFixedBuffer_t xFixedBuffer = { .pBuffer = xNetworkBuffer, .size = MQTT_AGENT_NETWORK_BUFFER_SIZE };
    static uint8_t staticQueueStorageArea[ MQTT_AGENT_COMMAND_QUEUE_LENGTH * sizeof( MQTTAgentCommand_t * ) ];
    static StaticQueue_t staticQueueStructure;

    MQTTAgentMessageInterface_t messageInterface =
    {
        .pMsgCtx        = NULL,
        .send           = Agent_MessageSend,
        .recv           = Agent_MessageReceive,
        .getCommand     = Agent_GetCommand,
        .releaseCommand = Agent_ReleaseCommand
    };

    LogDebug( ( "Creating command queue." ) );
    xCommandQueue.queue = xQueueCreateStatic( MQTT_AGENT_COMMAND_QUEUE_LENGTH,
                                              sizeof( MQTTAgentCommand_t * ),
                                              staticQueueStorageArea,
                                              &staticQueueStructure );
    configASSERT( xCommandQueue.queue );
    messageInterface.pMsgCtx = &xCommandQueue;

    /* Initialize the task pool. */
    Agent_InitializePool();

    /* Fill in Transport Interface send and receive function pointers. */
    xTransport.pNetworkContext = pxNetworkContext;
    xTransport.send = mbedtls_transport_send;
    xTransport.recv = mbedtls_transport_recv;

    /* Initialize MQTT library. */
    xReturn = MQTTAgent_Init( &xGlobalMqttAgentContext,
                              &messageInterface,
                              &xFixedBuffer,
                              &xTransport,
                              prvGetTimeMs,
                              prvIncomingPublishCallback,
                              /* Context to pass into the callback. Passing the pointer to subscription array. */
                              xGlobalSubscriptionList );

    return xReturn;
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvMQTTConnect( bool xCleanSession,
                                    MqttConnectCtx_t * pxConnectCtx )
{
    MQTTStatus_t xResult;
    bool xSessionPresent = false;

    /* Start with a clean session i.e. direct the MQTT broker to discard any
     * previous session data. Also, establishing a connection with clean session
     * will ensure that the broker does not store any data when this client
     * gets disconnected. */
    pxConnectCtx->xCInfo.cleanSession = xCleanSession;

    /* Send MQTT CONNECT packet to broker. MQTT's Last Will and Testament feature
     * is not used in this demo, so it is passed as NULL. */
    xResult = MQTT_Connect( &( xGlobalMqttAgentContext.mqttContext ),
                            &( pxConnectCtx->xCInfo ),
                            NULL,
                            CONNACK_RECV_TIMEOUT_MS,
                            &xSessionPresent );

    LogInfo( ( "Session present: %d\n", xSessionPresent ) );

    /* Resume a session if desired. */
    if( ( xResult == MQTTSuccess ) && ( xCleanSession == false ) )
    {
        xResult = MQTTAgent_ResumeSession( &xGlobalMqttAgentContext, xSessionPresent );

        /* Resubscribe to all the subscribed topics. */
        if( ( xResult == MQTTSuccess ) && ( xSessionPresent == false ) )
        {
            xResult = prvHandleResubscribe();
        }
    }

    return xResult;
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvHandleResubscribe( void )
{
    MQTTStatus_t xResult = MQTTBadParameter;
    uint32_t ulIndex = 0U;
    uint16_t usNumSubscriptions = 0U;

    /* These variables need to stay in scope until command completes. */
    static MQTTAgentSubscribeArgs_t xSubArgs = { 0 };
    static MQTTSubscribeInfo_t xSubInfo[ SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS ] = { 0 };
    static MQTTAgentCommandInfo_t xCommandParams = { 0 };

    /* Loop through each subscription in the subscription list and add a subscribe
     * command to the command queue. */
    for( ulIndex = 0U; ulIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; ulIndex++ )
    {
        /* Check if there is a subscription in the subscription list. This demo
         * doesn't check for duplicate subscriptions. */
        if( xGlobalSubscriptionList[ ulIndex ].usFilterStringLength != 0 )
        {
            xSubInfo[ usNumSubscriptions ].pTopicFilter = xGlobalSubscriptionList[ ulIndex ].pcSubscriptionFilterString;
            xSubInfo[ usNumSubscriptions ].topicFilterLength = xGlobalSubscriptionList[ ulIndex ].usFilterStringLength;

            /* QoS1 is used for all the subscriptions in this demo. */
            xSubInfo[ usNumSubscriptions ].qos = MQTTQoS1;

            LogInfo( ( "Resubscribe to the topic %.*s will be attempted.",
                       xSubInfo[ usNumSubscriptions ].topicFilterLength,
                       xSubInfo[ usNumSubscriptions ].pTopicFilter ) );

            usNumSubscriptions++;
        }
    }

    if( usNumSubscriptions > 0U )
    {
        xSubArgs.pSubscribeInfo = xSubInfo;
        xSubArgs.numSubscriptions = usNumSubscriptions;

        /* The block time can be 0 as the command loop is not running at this point. */
        xCommandParams.blockTimeMs = 0U;
        xCommandParams.cmdCompleteCallback = prvSubscriptionCommandCallback;
        xCommandParams.pCmdCompleteCallbackContext = ( void * ) &xSubArgs;

        /* Enqueue subscribe to the command queue. These commands will be processed only
         * when command loop starts. */
        xResult = MQTTAgent_Subscribe( &xGlobalMqttAgentContext, &xSubArgs, &xCommandParams );
    }
    else
    {
        /* Mark the resubscribe as success if there is nothing to be subscribed. */
        xResult = MQTTSuccess;
    }

    if( xResult != MQTTSuccess )
    {
        LogError( ( "Failed to enqueue the MQTT subscribe command. xResult=%s.",
                    MQTT_Status_strerror( xResult ) ) );
    }

    return xResult;
}

/*-----------------------------------------------------------*/

static void prvSubscriptionCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                            MQTTAgentReturnInfo_t * pxReturnInfo )
{
    size_t lIndex = 0;
    MQTTAgentSubscribeArgs_t * pxSubscribeArgs = ( MQTTAgentSubscribeArgs_t * ) pxCommandContext;

    /* If the return code is success, no further action is required as all the topic filters
     * are already part of the subscription list. */
    if( pxReturnInfo->returnCode != MQTTSuccess )
    {
        /* Check through each of the suback codes and determine if there are any failures. */
        for( lIndex = 0; lIndex < pxSubscribeArgs->numSubscriptions; lIndex++ )
        {
            /* This demo doesn't attempt to resubscribe in the event that a SUBACK failed. */
            if( pxReturnInfo->pSubackCodes[ lIndex ] == MQTTSubAckFailure )
            {
                LogError( ( "Failed to resubscribe to topic %.*s.",
                            pxSubscribeArgs->pSubscribeInfo[ lIndex ].topicFilterLength,
                            pxSubscribeArgs->pSubscribeInfo[ lIndex ].pTopicFilter ) );
                /* Remove subscription callback for unsubscribe. */
                submgr_removeSubscription( xGlobalSubscriptionList,
                                    pxSubscribeArgs->pSubscribeInfo[ lIndex ].pTopicFilter,
                                    pxSubscribeArgs->pSubscribeInfo[ lIndex ].topicFilterLength );
            }
        }

        /* Hit an assert as some of the tasks won't be able to proceed correctly without
         * the subscriptions. This logic will be updated with exponential backoff and retry.  */
        configASSERT( pdTRUE );
    }
}

/*-----------------------------------------------------------*/

static void prvIncomingPublishCallback( MQTTAgentContext_t * pMqttAgentContext,
                                        uint16_t packetId,
                                        MQTTPublishInfo_t * pxPublishInfo )
{
    bool xPublishHandled = false;
    char cOriginalChar, * pcLocation;

    ( void ) packetId;

    /* Fan out the incoming publishes to the callbacks registered using
     * subscription manager. */
    xPublishHandled = submgr_handleIncomingPublish( ( SubscriptionElement_t * ) pMqttAgentContext->pIncomingCallbackContext,
                                                    pxPublishInfo );

    /* If there are no callbacks to handle the incoming publishes,
     * handle it as an unsolicited publish. */
    if( xPublishHandled == false )
    {
        /* Ensure the topic string is terminated for printing.  This will over-
         * write the message ID, which is restored afterwards. */
        pcLocation = ( char * ) &( pxPublishInfo->pTopicName[ pxPublishInfo->topicNameLength ] );
        cOriginalChar = *pcLocation;
        *pcLocation = 0x00;
        LogWarn( ( "WARN:  Received an unsolicited publish from topic %s", pxPublishInfo->pTopicName ) );
        *pcLocation = cOriginalChar;
    }
}

/*-----------------------------------------------------------*/

void vMQTTAgentTask( void * pvParameters )
{
    ( void ) pvParameters;

    NetworkContext_t * pxNetworkContext = NULL;
    TlsTransportStatus_t xTlsStatus = TLS_TRANSPORT_CONNECT_FAILURE;
    MQTTStatus_t xMQTTStatus;
    MqttConnectCtx_t xConnectCtx = { 0 };
    BaseType_t xExitFlag = pdFALSE;

    /* Miscellaneous initialization. */
    ulGlobalEntryTimeMs = prvGetTimeMs();

    xInitializeMqttConnectCtx( &xConnectCtx );

    /* Initialize subscription manager */
    submgr_init();

    if( pxNetworkContext == NULL )
    {
        pxNetworkContext = mbedtls_transport_allocate( &xLwipTransportInterface );
    }

    configASSERT( pxNetworkContext != NULL );

    /* Initialize the MQTT context with the buffer and transport interface. */
    xMQTTStatus = prvMQTTInit( pxNetworkContext );

    configASSERT( xMQTTStatus == MQTTSuccess );

    ( void ) xEventGroupSetBits( xSystemEvents, EVT_MASK_MQTT_INIT );

    /* Outer Reconnect loop */
    while( xExitFlag != pdTRUE )
    {
        BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;
        BackoffAlgorithmContext_t xReconnectParams = { 0 };

        /* Initialize backoff algorithm with jitter */
        BackoffAlgorithm_InitializeParams( &xReconnectParams,
                                           RETRY_BACKOFF_BASE_S,
                                           RETRY_MAX_BACKOFF_DELAY_S,
                                           RETRY_MAX_ATTEMPTS );

        /* Connect a socket to the broker with retries */
        while( xTlsStatus != TLS_TRANSPORT_SUCCESS &&
               xBackoffAlgStatus == BackoffAlgorithmSuccess )
        {
            LogInfo( ( "Creating a TLS connection to %s:%d.",
                       xConnectCtx.pcMqttEndpointAddress,
                       xConnectCtx.ulMqttPort ) );

            xTlsStatus = mbedtls_transport_connect( pxNetworkContext,
                                                    xConnectCtx.pcMqttEndpointAddress,
                                                    ( uint16_t ) xConnectCtx.ulMqttPort,
                                                    &( xConnectCtx.xNetworkCredentials ),
                                                    SEND_RECV_TIMEOUT_MS,
                                                    SEND_RECV_TIMEOUT_MS );

            if( xTlsStatus != TLS_TRANSPORT_SUCCESS )
            {
                uint16_t usNextRetryBackOff = 0U;

                /* Get back-off value (in seconds) for the next connection retry. */
                xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &xReconnectParams,
                                                                     uxRand(),
                                                                     &usNextRetryBackOff );

                if( xBackoffAlgStatus == BackoffAlgorithmSuccess )
                {
                    LogWarn( ( "Connection to the broker failed. "
                               "Retrying connection in %hu seconds.",
                               usNextRetryBackOff ) );
                    vTaskDelay( pdMS_TO_TICKS( 1000 * usNextRetryBackOff ) );
                }
                else if( xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted )
                {
                    LogError( ( "Connection to the broker failed, all attempts exhausted." ) );
                    xExitFlag = pdTRUE;
                }
            }
        }

        if( xTlsStatus == TLS_TRANSPORT_SUCCESS )
        {
            /* Form an MQTT connection without a persistent session. */
            xMQTTStatus = prvMQTTConnect( true, &xConnectCtx );
        }
        else
        {
            xMQTTStatus = MQTTKeepAliveTimeout;
        }

        if( xMQTTStatus == MQTTSuccess )
        {
            ( void ) xEventGroupSetBits( xSystemEvents, EVT_MASK_MQTT_CONNECTED );

            /* MQTTAgent_CommandLoop() is effectively the agent implementation.  It
             * will manage the MQTT protocol until such time that an error occurs,
             * which could be a disconnect.  If an error occurs the MQTT context on
             * which the error happened is returned so there can be an attempt to
             * clean up and reconnect however the application writer prefers. */
            xMQTTStatus = MQTTAgent_CommandLoop( &xGlobalMqttAgentContext );

            ( void ) xEventGroupClearBits( xSystemEvents, EVT_MASK_MQTT_CONNECTED );

            mbedtls_transport_disconnect( pxNetworkContext );
        }
    }

    vDeinitMqttConnectCtx( &xConnectCtx );

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
