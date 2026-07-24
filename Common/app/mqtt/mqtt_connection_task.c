/**
  ******************************************************************************
  * @file           : mqtt_connection_task.c
  * @brief          : Task implementation of the MQTT connection state machine
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
#include "logging_levels.h"
#define LOG_LEVEL    LOG_INFO
#include "logging.h"

#include "mqtt_agent_task.h"
#include "greengrass_discovery_task.h"
#include "mqtt_connection_task.h"
#include "tls_transport_config.h"
#include "sys_evt.h"
#include "backoff_algorithm.h"

/**
 * Maximum number of attempts to reconnect to the GreenGrass core device
 * before attempting a Greengrass discovery.
 */
#define MAX_GG_FIRST_CONNECTION_ATTEMPTS        2

/**
 * Maximum number of attempts to reconnect to the GreenGrass core device
 * before connecting to the cloud.
 */
#define MAX_GG_CONNECTION_ATTEMPTS              5

#define MQTT_AGENT_TASK_PRIORITY                11
#define GG_DISCOVERY_TASK_PRIORITY              11

extern void vMQTTAgentTask( void * pvParameters);
extern void vGGDiscoveryTask( void * pvParameters);

static void setGGConnectionContext( MQTTConnectionContext_t *ConnectionConext,  uint32_t maxBackOffAttempts )
{
    ConnectionConext->endpointLabel = CS_CORE_GG_ENDPOINT;
    ConnectionConext->portLabel = CS_CORE_GG_PORT;
    ConnectionConext->caLabel = TLS_ROOT_GG_CA_CERT_LABEL;
    ConnectionConext->maxBackoffAttempts = maxBackOffAttempts;
    ConnectionConext->mqttAgentConnected = false;
}

static void setCloudConnectionContext( MQTTConnectionContext_t *ConnectionConext, uint32_t maxBackOffAttempts )
{
    ConnectionConext->endpointLabel = CS_CORE_MQTT_ENDPOINT;
    ConnectionConext->portLabel = CS_CORE_MQTT_PORT;
    ConnectionConext->caLabel = TLS_ROOT_CA_CERT_LABEL;
    ConnectionConext->maxBackoffAttempts = maxBackOffAttempts;
    ConnectionConext->mqttAgentConnected = false;
}

void vMQTTConnectionTask( void * pvParameters ){
    BaseType_t xResult;

    MQTTConnectionContext_t ConnectionCtx;
    GGDiscoveryContext xGGContext;
    
    setGGConnectionContext(&ConnectionCtx, MAX_GG_FIRST_CONNECTION_ATTEMPTS);

    LogInfo("Connecting to Greengrass Core device using NVM config");
    xResult = xTaskCreate( vMQTTAgentTask, "MQTTAgent", 1024, &ConnectionCtx, MQTT_AGENT_TASK_PRIORITY, NULL );
    configASSERT( xResult == pdTRUE );

    ( void ) xEventGroupWaitBits( xSystemEvents,
                                EVT_MASK_MQTT_AGENT_FINISHED,
                                0x00,
                                pdTRUE,
                                portMAX_DELAY );

    /* Attempt Greengrass discovery if direct connection fails */
    if (ConnectionCtx.mqttAgentConnected == false)
    {
        LogWarn( "Connection to Greengrass Core device using NVM config failed. Initiating GreenGrass discovery." );

        /* Attempt Greengrass discovery connection */ 
        xResult = xTaskCreate( vGGDiscoveryTask, "GGDiscovery", 1024, &xGGContext, GG_DISCOVERY_TASK_PRIORITY , NULL );
        configASSERT( xResult == pdTRUE );

        ( void ) xEventGroupWaitBits( xSystemEvents,
                                EVT_MASK_GG_DISCOVERY_PERFORMED,
                                0x00,
                                pdTRUE,
                                portMAX_DELAY );

        /* If GG Discovery successful, attempt connection to the GG core device */
        if( xGGContext.ggDiscoverySuccess == true ){
            LogInfo("Greengrass Discovery succeeded. Connecting to Greengrass Core device");
            setGGConnectionContext(&ConnectionCtx, MAX_GG_CONNECTION_ATTEMPTS);

            xResult = xTaskCreate( vMQTTAgentTask, "MQTTAgent", 1024, &ConnectionCtx, MQTT_AGENT_TASK_PRIORITY, NULL );
            configASSERT( xResult == pdTRUE );

            ( void ) xEventGroupWaitBits( xSystemEvents,
                                    EVT_MASK_MQTT_AGENT_FINISHED,
                                    0x00,
                                    pdTRUE,
                                    portMAX_DELAY );

            /* If connection to the GG Core device failed, connect to Cloud */
            if (ConnectionCtx.mqttAgentConnected == false){
                LogWarn("Connection to Greengrass failed. Connecting to Cloud");
                setCloudConnectionContext(&ConnectionCtx, BACKOFF_ALGORITHM_RETRY_FOREVER);

                xResult = xTaskCreate( vMQTTAgentTask, "MQTTAgent", 1024, &ConnectionCtx, MQTT_AGENT_TASK_PRIORITY, NULL );
                configASSERT( xResult == pdTRUE );

                ( void ) xEventGroupWaitBits( xSystemEvents,
                                        EVT_MASK_MQTT_AGENT_FINISHED,
                                        0x00,
                                        pdTRUE,
                                        portMAX_DELAY );
            }
        }
        /* If GG Discovery failed, connect to Cloud */
        else
        {
            LogWarn("Greengrass Discovery failed. Connecting to Cloud");
            setCloudConnectionContext(&ConnectionCtx, BACKOFF_ALGORITHM_RETRY_FOREVER);

            xResult = xTaskCreate( vMQTTAgentTask, "MQTTAgent", 1024, &ConnectionCtx, MQTT_AGENT_TASK_PRIORITY, NULL );
            configASSERT( xResult == pdTRUE );

            ( void ) xEventGroupWaitBits( xSystemEvents,
                                    EVT_MASK_MQTT_AGENT_FINISHED,
                                    0x00,
                                    pdTRUE,
                                    portMAX_DELAY );
        }
    }
    LogInfo( "Terminating MqttConnecionTask." );
    vTaskDelete(NULL);
}
