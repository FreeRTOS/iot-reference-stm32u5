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
        pubInfo.qos = 0;
        pubInfo.retain = false;
        pubInfo.dup = false;
        pubInfo.pTopicName = topic;
        pubInfo.topicNameLength = topicLength;
        pubInfo.pPayload = message;
        pubInfo.payloadLength = messageLength;

        mqttStatus = MQTT_Publish( globalCoreMqttContext,
                                   &pubInfo,
                                   MQTT_GetPacketId( globalCoreMqttContext ) );
        success = mqttStatus == MQTTSuccess;
    }
    return success;
}

bool mqttWrapper_subscribe( char * topic, size_t topicLength )
{
    bool success = false;
    assert( globalCoreMqttContext != NULL );

    success = mqttWrapper_isConnected();
    if( success )
    {
        MQTTStatus_t mqttStatus = MQTTSuccess;
        MQTTSubscribeInfo_t subscribeInfo = { 0 };
        subscribeInfo.qos = 0;
        subscribeInfo.pTopicFilter = topic;
        subscribeInfo.topicFilterLength = topicLength;

        mqttStatus = MQTT_Subscribe( globalCoreMqttContext,
                                     &subscribeInfo,
                                     1,
                                     MQTT_GetPacketId(
                                         globalCoreMqttContext ) );
        success = mqttStatus == MQTTSuccess;
    }
    return success;
}
