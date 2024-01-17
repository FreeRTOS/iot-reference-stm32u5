/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#ifndef MQTT_WRAPPER_H
#define MQTT_WRAPPER_H

#include "core_mqtt.h"

void mqttWrapper_setCoreMqttContext( MQTTContext_t * mqttContext );

MQTTContext_t * mqttWrapper_getCoreMqttContext( void );

void mqttWrapper_setThingName( char * thingName, size_t thingNameLength );

void mqttWrapper_getThingName( char * thingNameBuffer,
                               size_t * thingNameLength );

bool mqttWrapper_connect( char * thingName, size_t thingNameLength );

bool mqttWrapper_isConnected( void );

bool mqttWrapper_publish( char * topic,
                          size_t topicLength,
                          uint8_t * message,
                          size_t messageLength );

bool mqttWrapper_subscribe( char * topic, size_t topicLength );

#endif
