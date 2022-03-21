/*
 * FreeRTOS V202011.00
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
 * https://www.FreeRTOS.org
 * https://aws.amazon.com/freertos
 *
 */

/**
 * @file subscription_manager.h
 * @brief Functions for managing MQTT subscriptions.
 */
#ifndef SUBSCRIPTION_MANAGER_H
#define SUBSCRIPTION_MANAGER_H

#include "mqtt_metrics.h"
#include "core_mqtt.h"
#include "mqtt_agent_task.h"

/**
 * @brief Maximum number of concurrent subscriptions.
 */
#ifndef MQTT_AGENT_MAX_SUBSCRIPTIONS
#define MQTT_AGENT_MAX_SUBSCRIPTIONS    10U
#endif /* MQTT_AGENT_MAX_SUBSCRIPTIONS */

/**
 * @brief Maximum number of callbacks that may be registered.
 */
#ifndef MQTT_AGENT_MAX_CALLBACKS
#define MQTT_AGENT_MAX_CALLBACKS    10U
#endif /* MQTT_AGENT_MAX_CALLBACKS */

/**
 * @brief Callback function called when receiving a publish.
 *
 * @param[in] pvIncomingPublishCallbackContext The incoming publish callback context.
 * @param[in] pxPublishInfo Deserialized publish information.
 */
typedef void (* IncomingPubCallback_t )( void * pvIncomingPublishCallbackContext,
                                         MQTTPublishInfo_t * pxPublishInfo );

/**
 * @brief An element in the list of subscriptions.
 *
 * This subscription manager implementation expects that the array of the
 * subscription elements used for storing subscriptions to be initialized to 0.
 *
 * @note This implementation allows multiple tasks to subscribe to the same topic.
 * In this case, another element is added to the subscription list, differing
 * in the intended publish callback. Also note that the topic filters are not
 * copied in the subscription manager and hence the topic filter strings need to
 * stay in scope until unsubscribed.
 */
typedef struct
{
    IncomingPubCallback_t pxIncomingPublishCallback;
    void * pvIncomingPublishCallbackContext;
    TaskHandle_t xTaskHandle;
    MQTTSubscribeInfo_t * pxSubInfo;
} SubCallbackElement_t;


/* @brief Add a callback for a given topic filter. Subscribe if not already subscribed.
 *
 * @param[in] xHandle Handle for the desired MQTT Agent Task instance.
 * @param[in] pcTopicFilter Topic filter string to subscribe to.
 * @param[in] xRequestedQoS Requested QoS for this subscription.
 * @param[in] pxIncomingPublishCallback Callback function for the subscription.
 * @param[in] pvIncomingPublishCallbackContext Context for the subscription callback.
 * @return `MQTTSuccess` if the subscription was added successfully.
 **/
MQTTStatus_t MqttAgent_SubscribeSync( MQTTAgentHandle_t xHandle,
                                      const char * pcTopicFilter,
                                      MQTTQoS_t xRequestedQoS,
                                      IncomingPubCallback_t pxCallback,
                                      void * pvCallbackCtx );

/* @brief Remove the specified callback from the given topic filter.
 * Unsubscribe from the specified topic is no other callback exist for the same filter.
 *
 * @param[in] xHandle Handle for the desired MQTT Agent Task instance.
 * @param[in] pcTopicFilter Topic filter string to subscribe to.
 * @param[in] pxIncomingPublishCallback Callback function for the subscription.
 * @param[in] pvIncomingPublishCallbackContext Context for the subscription callback.
 * @return `MQTTSuccess` if the subscription was successfully removed.
 **/
MQTTStatus_t MqttAgent_UnSubscribeSync( MQTTAgentHandle_t xHandle,
                                        const char * pcTopicFilter,
                                        IncomingPubCallback_t pxCallback,
                                        void * pvCallbackCtx );

#endif /* SUBSCRIPTION_MANAGER_H */
