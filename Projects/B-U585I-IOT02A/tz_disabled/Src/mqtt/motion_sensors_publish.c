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

#define LOG_LEVEL LOG_INFO

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
#include "mqtt_agent_task.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

/* Sensor includes */
#include "b_u585i_iot02a_motion_sensors.h"

/**
 * @brief Size of statically allocated buffers for holding topic names and
 * payloads.
 */
#define MQTT_PUBLISH_MAX_LEN              ( 200 )
#define MQTT_PUBLISH_PERIOD_MS            ( 1000 )
#define MQTT_PUBLISH_TOPIC                "/stm32u5/motion_sensor_data"
#define MQTT_PUBLISH_BLOCK_TIME_MS        ( 50 )
#define MQTT_PUBLISH_NOTIFICATION_WAIT_MS ( 10*1000 )
#define MQTT_NOTIFY_IDX                   ( 1 )
#define MQTT_PUBLISH_QOS                  ( 0 )


extern MQTTAgentContext_t xGlobalMqttAgentContext;

/*-----------------------------------------------------------*/
/**
 * @brief Defines the structure to use as the command callback context in this
 * demo.
 */
struct MQTTAgentCommandContext
{
    MQTTStatus_t xReturnStatus;
    TaskHandle_t xTaskToNotify;
    uint32_t ulNotificationValue;
};


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
        xTaskNotify( pxCommandContext->xTaskToNotify,
                     pxCommandContext->ulNotificationValue,
                     eSetValueWithOverwrite );
    }
}

/*-----------------------------------------------------------*/
static BaseType_t prvWaitForCommandAcknowledgment( uint32_t * pulNotifiedValue )
{
    BaseType_t xReturn;

    /* Wait for this task to get notified, passing out the value it gets
     * notified with. */
    xReturn = xTaskNotifyWait( 0,
                               pdTRUE,
                               pulNotifiedValue,
                               pdMS_TO_TICKS( MQTT_PUBLISH_NOTIFICATION_WAIT_MS ) );
    return xReturn;
}

/*-----------------------------------------------------------*/
static BaseType_t prvPublishAndWaitForAck( const char * pcTopic,
                                           const void * pvPublishData,
                                           size_t xPublishDataLen )
{
    MQTTStatus_t xStatus = 0xFFFFFFFF;
    BaseType_t xResult = pdPASS;
    uint32_t ulNotifications = 0;
    uint32_t ulAckNotification = 1u;

    configASSERT( pcTopic != NULL );
    configASSERT( pvPublishData != NULL );
    configASSERT( xPublishDataLen > 0 );

    MQTTPublishInfo_t xPublishInfo;
    memset( ( void * ) &xPublishInfo, 0x00, sizeof( xPublishInfo ) );
    xPublishInfo.qos = MQTT_PUBLISH_QOS;
    xPublishInfo.pTopicName = pcTopic;
    xPublishInfo.topicNameLength = strlen( pcTopic );
    xPublishInfo.pPayload = pvPublishData;
    xPublishInfo.payloadLength = xPublishDataLen;

    MQTTAgentCommandContext_t xCommandContext;
    memset( ( void * ) &xCommandContext, 0x00, sizeof( xCommandContext ) );
    xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();
    xCommandContext.ulNotificationValue = ulAckNotification;

    MQTTAgentCommandInfo_t xCommandParams;
    xCommandParams.blockTimeMs = MQTT_PUBLISH_BLOCK_TIME_MS;
    xCommandParams.cmdCompleteCallback = prvPublishCommandCallback;
    xCommandParams.pCmdCompleteCallbackContext = &xCommandContext;

    LogInfo("Publishing message");
    /* Clear the notification index */
    xStatus = MQTTAgent_Publish( &xGlobalMqttAgentContext,
                                 &xPublishInfo,
                                 &xCommandParams );
    if( xStatus == MQTTSuccess )
    {
        prvWaitForCommandAcknowledgment( &ulNotifications );
        if( ulNotifications != ulAckNotification )
        {
            LogError( "Timed out while waiting for ACK on publish to %s", pcTopic );
            xResult = pdFAIL;
        }
    }
    else
    {
        LogError("Failed to publish to %s", pcTopic );
        xResult = pdFAIL;
    }

    return xResult;
}

/*-----------------------------------------------------------*/
static BaseType_t xInitSensors( void )
{
    int32_t lBspError = BSP_ERROR_NONE;

    /* Gyro + Accelerometer*/
    lBspError = BSP_MOTION_SENSOR_Init( 0, MOTION_GYRO | MOTION_ACCELERO );
    lBspError |= BSP_MOTION_SENSOR_Enable( 0, MOTION_GYRO );
    lBspError |= BSP_MOTION_SENSOR_Enable( 0, MOTION_ACCELERO );
    lBspError |= BSP_MOTION_SENSOR_SetOutputDataRate( 0, MOTION_GYRO, 1.0f );
    lBspError |= BSP_MOTION_SENSOR_SetOutputDataRate( 0, MOTION_ACCELERO, 1.0f );

    /* Magnetometer */
    lBspError |= BSP_MOTION_SENSOR_Init( 1, MOTION_MAGNETO );
    lBspError |= BSP_MOTION_SENSOR_Enable( 1, MOTION_MAGNETO );
    lBspError |= BSP_MOTION_SENSOR_SetOutputDataRate( 1, MOTION_MAGNETO, 1.0f );

    return ( lBspError == BSP_ERROR_NONE ? pdTRUE : pdFALSE );
}

/*-----------------------------------------------------------*/
void Task_MotionSensorsPublish( void * pvParameters )
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

    LogInfo( "Waiting until MQTT Agent is ready" );
    vSleepUntilMQTTAgentReady();
    LogInfo( "MQTT Agent is ready. Resuming..." );


    while( xExitFlag == pdFALSE )
    {
        /* Interpret sensor data */
        int32_t lBspError = BSP_ERROR_NONE;
        BSP_MOTION_SENSOR_Axes_t xAcceleroAxes, xGyroAxes, xMagnetoAxes;

        lBspError = BSP_MOTION_SENSOR_GetAxes( 0, MOTION_GYRO, &xGyroAxes );
        lBspError |= BSP_MOTION_SENSOR_GetAxes( 0, MOTION_ACCELERO, &xAcceleroAxes );
        lBspError |= BSP_MOTION_SENSOR_GetAxes( 1, MOTION_MAGNETO, &xMagnetoAxes );

        if( lBspError == BSP_ERROR_NONE )
        {
            int bytesWritten = snprintf( payloadBuf,
                                     MQTT_PUBLISH_MAX_LEN,
                                     "{"
                                          "\"acceleration_mG\":"
                                            "{"
                                                "\"x\": %d,"
                                                "\"y\": %d,"
                                                "\"z\": %d"
                                            "},"
                                          "\"gyro_mDPS\":"
                                            "{"
                                                "\"x\": %d,"
                                                "\"y\": %d,"
                                                "\"z\": %d"
                                            "},"
                                          "\"magnetism_mGauss\":"
                                            "{"
                                                "\"x\": %d,"
                                                "\"y\": %d,"
                                                "\"z\": %d"
                                            "}"
                                     "}",
                                     xAcceleroAxes.x, xAcceleroAxes.y, xAcceleroAxes.z,
                                     xGyroAxes.x, xGyroAxes.y, xGyroAxes.z,
                                     xMagnetoAxes.x, xMagnetoAxes.y, xMagnetoAxes.z );

            if( bytesWritten < MQTT_PUBLISH_MAX_LEN )
            {
                xResult = prvPublishAndWaitForAck( MQTT_PUBLISH_TOPIC,
                                                   payloadBuf,
                                                   bytesWritten );
                if( xResult != pdPASS )
                {
                    LogError( "Failed to publish motion sensor data" );
                }
            }
        }

        vTaskDelay( pdMS_TO_TICKS( MQTT_PUBLISH_PERIOD_MS ) );
    }
}
