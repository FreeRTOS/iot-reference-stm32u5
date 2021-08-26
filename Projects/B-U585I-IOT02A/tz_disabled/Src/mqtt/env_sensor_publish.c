/*
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Derived from simple_sub_pub_demo.c
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


#include "logging_levels.h"
/* define LOG_LEVEL here if you want to modify the logging level from the default */

#define LOG_LEVEL LOG_ERROR

#include "logging.h"


/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* For I2c mutex */
#include "main.h"

/* MQTT library includes. */
#include "core_mqtt.h"

/* MQTT agent include. */
#include "core_mqtt_agent.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

/* Sensor includes */
#include "b_u585i_iot02a_env_sensors.h"

/**
 * @brief Size of statically allocated buffers for holding topic names and
 * payloads.
 */
#define MQTT_PUBLISH_MAX_LEN              ( 256 )
#define MQTT_PUBLISH_FREQUENCY_HZ         ( 1 )
#define MQTT_PUBLISH_TOPIC                "/stm32u5/env_sensor_data"
#define MQTT_PUBLISH_BLOCK_TIME_MS        ( 2000 )
#define MQTT_NOTIFY_IDX                   ( 1 )
#define MQTT_PUBLISH_QOS                  ( 0 )

/*-----------------------------------------------------------*/

/**
 * @brief Defines the structure to use as the command callback context in this
 * demo.
 */
struct MQTTAgentCommandContext
{
    MQTTStatus_t xReturnStatus;
    TaskHandle_t xTaskToNotify;
};

typedef struct
{
    float_t fTemperature0;
    float_t fTemperature1;
    float_t fHumidity;
    float_t fBarometricPressure;
} EnvironmentalSensorData_t;


/*-----------------------------------------------------------*/

static void prvSensorPublishTask( void * pvParameters );

/*-----------------------------------------------------------*/

extern MQTTAgentContext_t xGlobalMqttAgentContext;

/*-----------------------------------------------------------*/

void vStartSensorPublishTask( void )
{
    BaseType_t xResult = pdFALSE;
    xResult = xTaskCreate( prvSensorPublishTask,
                           "ESensorPub",
                           4096,
                           NULL,
                           10,
                           NULL );
    configASSERT( xResult == pdTRUE );
}

/*-----------------------------------------------------------*/

static void prvPublishCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                       MQTTAgentReturnInfo_t * pxReturnInfo )
{
    configASSERT( pxCommandContext != NULL );
    configASSERT( pxReturnInfo != NULL );

    pxCommandContext->xReturnStatus = pxReturnInfo->returnCode;

    if( pxCommandContext->xTaskToNotify != NULL )
    {
        /* Send the context's ulNotificationValue as the notification value so
         * the receiving task can check the value it set in the context matches
         * the value it receives in the notification. */
        ( void ) xTaskNotifyGiveIndexed( pxCommandContext->xTaskToNotify,
                                         MQTT_NOTIFY_IDX );
    }
}

/*-----------------------------------------------------------*/

static BaseType_t prvPublishAndWaitForAck( const char * pcTopic,
                                           const void * pvPublishData,
                                           size_t xPublishDataLen )
{
    BaseType_t xResult = pdFALSE;
    MQTTStatus_t xStatus;

    configASSERT( pcTopic != NULL );
    configASSERT( pvPublishData != NULL );
    configASSERT( xPublishDataLen > 0 );

    MQTTPublishInfo_t xPublishInfo =
    {
        .qos = MQTT_PUBLISH_QOS,
        .retain = 0,
        .dup = 0,
        .pTopicName = pcTopic,
        .topicNameLength = strlen( pcTopic ),
        .pPayload = pvPublishData,
        .payloadLength = xPublishDataLen
    };

    MQTTAgentCommandContext_t xCommandContext =
    {
        .xTaskToNotify = xTaskGetCurrentTaskHandle(),
        .xReturnStatus = MQTTIllegalState,
    };

    MQTTAgentCommandInfo_t xCommandParams =
    {
        .blockTimeMs = MQTT_PUBLISH_BLOCK_TIME_MS,
        .cmdCompleteCallback = prvPublishCommandCallback,
        .pCmdCompleteCallbackContext =  &xCommandContext
    };

    /* Clear the notification index */
    xStatus = MQTTAgent_Publish( &xGlobalMqttAgentContext,
                                 &xPublishInfo,
                                 &xCommandParams );

    if( xStatus == MQTTSuccess )
    {
        xResult = ulTaskNotifyTakeIndexed( MQTT_NOTIFY_IDX,
                                           pdTRUE,
                                           pdMS_TO_TICKS( MQTT_PUBLISH_BLOCK_TIME_MS ) );

        if( xResult == 0 )
        {
            LogError( "Timed out while waiting for publish ACK or Sent event. xTimeout = %d",
                      pdMS_TO_TICKS( MQTT_PUBLISH_BLOCK_TIME_MS ) );
            xResult = pdFALSE;
        }
        else if( xCommandContext.xReturnStatus != MQTTSuccess )
        {
            LogError( "MQTT Agent returned error code: %d during publish operation.",
                      xCommandContext.xReturnStatus );
            xResult = pdFALSE;
        }
    }
    else
    {
        LogError( "MQTTAgent_Publish returned error code: %d.",
                  xStatus );
    }

    return xResult;
}

/*-----------------------------------------------------------*/

static BaseType_t xInitSensors( void )
{
    int32_t lBspError = BSP_ERROR_NONE;

    lBspError = BSP_ENV_SENSOR_Init( 0, ENV_TEMPERATURE );

    lBspError |= BSP_ENV_SENSOR_Init( 0, ENV_HUMIDITY );

    lBspError |= BSP_ENV_SENSOR_Init( 1, ENV_TEMPERATURE );

    lBspError |= BSP_ENV_SENSOR_Init( 1, ENV_PRESSURE );

    lBspError |= BSP_ENV_SENSOR_Enable( 0, ENV_TEMPERATURE );

    lBspError |= BSP_ENV_SENSOR_Enable( 0, ENV_HUMIDITY );

    lBspError |= BSP_ENV_SENSOR_Enable( 1, ENV_TEMPERATURE );

    lBspError |= BSP_ENV_SENSOR_Enable( 1, ENV_PRESSURE );

    lBspError |= BSP_ENV_SENSOR_SetOutputDataRate( 0, ENV_TEMPERATURE, 1.0f );

    lBspError |= BSP_ENV_SENSOR_SetOutputDataRate( 0, ENV_HUMIDITY, 1.0f );

    lBspError |= BSP_ENV_SENSOR_SetOutputDataRate( 1, ENV_TEMPERATURE, 1.0f );

    lBspError |= BSP_ENV_SENSOR_SetOutputDataRate( 1, ENV_PRESSURE, 1.0f );

    return ( lBspError == BSP_ERROR_NONE ? pdTRUE : pdFALSE );
}

static BaseType_t xUpdateSensorData( EnvironmentalSensorData_t * pxData )
{
    int32_t lBspError = BSP_ERROR_NONE;

    if( xSemaphoreTake( xHwMutexI2C2, portMAX_DELAY ) == pdTRUE )
    {
        lBspError = BSP_ENV_SENSOR_GetValue( 0, ENV_TEMPERATURE, &pxData->fTemperature0 );
        lBspError |= BSP_ENV_SENSOR_GetValue( 0, ENV_HUMIDITY, &pxData->fHumidity );
        lBspError |= BSP_ENV_SENSOR_GetValue( 1, ENV_TEMPERATURE, &pxData->fTemperature1 );
        lBspError |= BSP_ENV_SENSOR_GetValue( 1, ENV_PRESSURE, &pxData->fBarometricPressure );

        (void) xSemaphoreGive( xHwMutexI2C2 );
    }
    else
    {
        LogError( "Failed to acquire xHwMutexI2C2." );
        lBspError = BSP_ERROR_UNKNOWN_FAILURE;
    }
    return ( lBspError == BSP_ERROR_NONE ? pdTRUE : pdFALSE );
}

/*-----------------------------------------------------------*/

extern UBaseType_t uxRand( void );

static void prvSensorPublishTask( void * pvParameters )
{
    (void) pvParameters;
    BaseType_t xResult = pdFALSE;
    BaseType_t xExitFlag = pdFALSE;
    char payloadBuf[ MQTT_PUBLISH_MAX_LEN ];

    xResult = xInitSensors();

    if( xResult != pdTRUE )
    {
        LogError("Error while initializing environmental sensors.");
        vTaskDelete( NULL );
    }

    while( xGlobalMqttAgentContext.mqttContext.connectStatus != MQTTConnected )
    {
        vTaskDelay(10 * 1000);
    }

    while( xExitFlag == pdFALSE )
    {
        TickType_t xTicksToWait = pdMS_TO_TICKS( MQTT_PUBLISH_BLOCK_TIME_MS );
        TimeOut_t xTimeOut;

        EnvironmentalSensorData_t xEnvData;
        xResult = xUpdateSensorData( &xEnvData );

        if( xResult == pdTRUE )
        {
            int bytesWritten = 0;

            vTaskSetTimeOutState( &xTimeOut );

            /* Write to */
            bytesWritten = snprintf( payloadBuf,
                                     MQTT_PUBLISH_MAX_LEN,
                                     "{ \"temperature0_deg_c\": %f, \"humidity\": %f, \"temperature1_deg_c\": %f, \"baro_pressure_mbar\": %f }",
                                     xEnvData.fTemperature0,
                                     xEnvData.fHumidity,
                                     xEnvData.fTemperature1,
                                     xEnvData.fBarometricPressure );

            if( bytesWritten < MQTT_PUBLISH_MAX_LEN )
            {
                xResult = prvPublishAndWaitForAck( MQTT_PUBLISH_TOPIC,
                                                   payloadBuf,
                                                   bytesWritten );
            }
            else
            {
                LogError("Not enough buffer space.");
            }

            if( xResult == pdTRUE )
            {
                LogDebug("Published sensor data: %s", payloadBuf );
            }
        }
        else
        {
            LogError("Failed to update sensor data.");
        }

        /* Adjust remaining tick count */
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE )
        {
            /* Wait until its time to poll the sensors again */
            vTaskDelay( xTicksToWait );
            LogDebug( "Slept for %d seconds.", xTicksToWait );
        }
    }
}
