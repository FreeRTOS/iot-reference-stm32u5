/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#include <assert.h>
#include <string.h>

#include "mqtt_wrapper.h"

static MQTTContext_t * globalCoreMqttContext = NULL;

#define MAX_THING_NAME_SIZE 128U
static char globalThingName[ MAX_THING_NAME_SIZE + 1 ];
static size_t globalThingNameLength = 0U;

/**
 * @brief Defines the structure to use as the command callback context in this
 * demo.
 */
struct MQTTAgentCommandContext
{
    MQTTStatus_t xReturnStatus;
    TaskHandle_t xTaskToNotify;
};

//static void handleIncomingMQTTMessage( char * topic,
//                                       size_t topicLength,
//                                       uint8_t * message,
//                                       size_t messageLength );
//
//static void mqttEventCallback( MQTTContext_t * mqttContext,
//                               MQTTPacketInfo_t * packetInfo,
//                               MQTTDeserializedInfo_t * deserializedInfo )
//{
//    char * topic = NULL;
//    size_t topicLength = 0U;
//    uint8_t * message = NULL;
//    size_t messageLength = 0U;
//
//    ( void ) mqttContext;
//
//    if( ( packetInfo->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
//    {
//        assert( deserializedInfo->pPublishInfo != NULL );
//        topic = ( char * ) deserializedInfo->pPublishInfo->pTopicName;
//        topicLength = deserializedInfo->pPublishInfo->topicNameLength;
//        message = ( uint8_t * ) deserializedInfo->pPublishInfo->pPayload;
//        messageLength = deserializedInfo->pPublishInfo->payloadLength;
//        handleIncomingMQTTMessage( topic, topicLength, message, messageLength );
//    }
//    else
//    {
//        /* Handle other packets. */
//        switch( packetInfo->type )
//        {
//            case MQTT_PACKET_TYPE_PUBACK:
//                printf( "PUBACK received with packet id: %u\n",
//                        ( unsigned int ) deserializedInfo->packetIdentifier );
//                break;
//
//            case MQTT_PACKET_TYPE_SUBACK:
//                printf( "SUBACK received with packet id: %u\n",
//                        ( unsigned int ) deserializedInfo->packetIdentifier );
//                break;
//
//            case MQTT_PACKET_TYPE_UNSUBACK:
//                printf( "UNSUBACK received with packet id: %u\n",
//                        ( unsigned int ) deserializedInfo->packetIdentifier );
//                break;
//            default:
//                printf( "Error: Unknown packet type received:(%02x).\n",
//                        packetInfo->type );
//        }
//    }
//}

static void handleIncomingMQTTMessage( char * topic,
                                       size_t topicLength,
                                       uint8_t * message,
                                       size_t messageLength )

{
    bool messageHandled = otaDemo_handleIncomingMQTTMessage( topic,
                                                             topicLength,
                                                             message,
                                                             messageLength );
    if( !messageHandled )
    {
        printf( "Unhandled incoming PUBLISH received on topic, message: "
                "%.*s\n%.*s\n",
                ( unsigned int ) topicLength,
                topic,
                ( unsigned int ) messageLength,
                ( char * ) message );
    }
}

void mqttWrapper_setCoreMqttContext( MQTTContext_t * mqttContext )
{
    globalCoreMqttContext = mqttContext;
}

MQTTContext_t * mqttWrapper_getCoreMqttContext( void )
{
    assert( globalCoreMqttContext != NULL );
    return globalCoreMqttContext;
}

void mqttWrapper_setThingName( char * thingName, size_t thingNameLength )
{
    strncpy( globalThingName, thingName, MAX_THING_NAME_SIZE );
    globalThingNameLength = thingNameLength;
}

void mqttWrapper_getThingName( char * thingNameBuffer,
                               size_t * thingNameLength )
{
    assert( globalThingName[ 0 ] != 0 );

    memcpy( thingNameBuffer, globalThingName, globalThingNameLength );
    thingNameBuffer[ globalThingNameLength ] = '\0';
    *thingNameLength = globalThingNameLength;
}

bool mqttWrapper_connect( char * thingName, size_t thingNameLength )
{
    MQTTConnectInfo_t connectInfo = { 0 };
    MQTTStatus_t mqttStatus = MQTTSuccess;
    bool sessionPresent = false;

    assert( globalCoreMqttContext != NULL );

    connectInfo.pClientIdentifier = thingName;
    connectInfo.clientIdentifierLength = thingNameLength;
    connectInfo.pUserName = NULL;
    connectInfo.userNameLength = 0U;
    connectInfo.pPassword = NULL;
    connectInfo.passwordLength = 0U;
    connectInfo.keepAliveSeconds = 60U;
    connectInfo.cleanSession = true;
    mqttStatus = MQTT_Connect( globalCoreMqttContext,
                               &connectInfo,
                               NULL,
                               5000U,
                               &sessionPresent );
    return mqttStatus == MQTTSuccess;
}

bool mqttWrapper_isConnected( void )
{
    bool isConnected = false;
    assert( globalCoreMqttContext != NULL );
    isConnected = globalCoreMqttContext->connectStatus == MQTTConnected;
    return isConnected;
}

static void prvPublishCommandCallback( MQTTAgentCommandContext_t * pCmdCallbackContext,
                                       MQTTAgentReturnInfo_t * pReturnInfo )
{
	/* TODO: Add handling. Notify tasks. */
}

bool mqttWrapper_publish( char * topic,
                          size_t topicLength,
                          uint8_t * message,
                          size_t messageLength )
{
    bool success = false;
    assert( globalCoreMqttContext != NULL );

    success = mqttWrapper_isConnected();
    if( success )
    {
        MQTTStatus_t mqttStatus = MQTTSuccess;
        MQTTPublishInfo_t pubInfo = { 0 };
        MQTTAgentHandle_t xAgentHandle = xGetMqttAgentHandle();
        pubInfo.qos = 0;
        pubInfo.retain = false;
        pubInfo.dup = false;
        pubInfo.pTopicName = topic;
        pubInfo.topicNameLength = topicLength;
        pubInfo.pPayload = message;
        pubInfo.payloadLength = messageLength;

		MQTTAgentCommandContext_t xCommandContext =
		{
			.xTaskToNotify = xTaskGetCurrentTaskHandle(),
			.xReturnStatus = MQTTIllegalState,
		};

		MQTTAgentCommandInfo_t xCommandParams =
		{
			.blockTimeMs                 = 1000,
			.cmdCompleteCallback         = prvPublishCommandCallback,
			.pCmdCompleteCallbackContext = &xCommandContext,
		};


		mqttStatus = MQTTAgent_Publish( xAgentHandle,
									 &pubInfo,
									 &xCommandParams );

        success = mqttStatus == MQTTSuccess;
    }
    return success;
}

void handleIncomingPublish( void * pvIncomingPublishCallbackContext,
                                         MQTTPublishInfo_t * pxPublishInfo )
{
	char * topic = NULL;
	size_t topicLength = 0U;
	uint8_t * message = NULL;
	size_t messageLength = 0U;

	LogError("OTA task got a message!!!");

	topic = ( char * ) pxPublishInfo->pTopicName;
	topicLength = pxPublishInfo->topicNameLength;
	message = ( uint8_t * ) pxPublishInfo->pPayload;
	messageLength = pxPublishInfo->payloadLength;
	handleIncomingMQTTMessage( topic, topicLength, message, messageLength );
}

bool mqttWrapper_subscribe( char * topic, size_t topicLength )
{
    bool success = false;
    assert( globalCoreMqttContext != NULL );

    success = mqttWrapper_isConnected();
    if( success )
    {
        MQTTStatus_t mqttStatus = MQTTSuccess;
        MQTTAgentHandle_t xAgentHandle = xGetMqttAgentHandle();

        mqttStatus = MqttAgent_SubscribeSync( xAgentHandle,
                                              topic,
                                              0,
											  handleIncomingPublish,
                                              NULL );

        configASSERT( mqttStatus == MQTTSuccess );

        success = mqttStatus == MQTTSuccess;
    }
    return success;
}
