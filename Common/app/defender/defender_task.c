/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

/*
 * Demo for showing how to use the Device Defender library's APIs. The Device
 * Defender library provides macros and helper functions for assembling MQTT
 * topics strings, and for determining whether an incoming MQTT message is
 * related to device defender.
 *
 * This demo subscribes to the device defender topics. It then collects metrics
 * for the open ports and sockets on the device using FreeRTOS+TCP. Additionally
 * the stack high water mark and task ids are collected for custom metrics.
 * These metrics are uses to generate a device defender report. The
 * report is then published, and the demo waits for a response from the device
 * defender service. Upon receiving the response or timing out, the demo
 * sleeps until the next iteration.
 *
 * This demo sets the report ID to xTaskGetTickCount(), which may collide if
 * the device is reset. Reports for a Thing with a previously used report ID
 * will be assumed to be duplicates and discarded by the Device Defender
 * service. The report ID needs to be unique per report sent with a given
 * Thing. We recommend using an increasing unique id such as the current
 * timestamp.
 */

#include "logging_levels.h"
#define LOG_LEVEL    LOG_INFO
#include "logging.h"


/* Standard includes. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "sys_evt.h"
#include "kvstore.h"

/* MQTT library includes. */
#include "core_mqtt_agent.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

/* Device Defender Client Library. */
#include "defender.h"

/* Metrics collector. */
#include "metrics_collector.h"

#include "cbor.h"

#define TCP_PORTS_MAX                      10
#define UDP_PORTS_MAX                      10
#define CONNECTIONS_MAX                    10
#define TASKS_MAX                          10
#define REPORT_BUFFER_SIZE                 1024

#define REPORT_MAJOR_VERSION               1
#define REPORT_MINOR_VERSION               0

#define MS_BETWEEN_REPORTS                 ( 5 * 60 * 1000U )      /* 5 Minute reporting interval */
#define RESPONSE_TIMEOUT_MS                ( 30 * 1000U )
#define MQTT_BLOCK_TIME_MS                 ( 10 * 1000U )

#define RESPONSE_REPORT_ID_FIELD           "reportId"
#define RESPONSE_REPORT_ID_FIELD_LENGTH    ( sizeof( RESPONSE_REPORT_ID_FIELD ) - 1 )

#define NUM_TOPIC_STRINGS                  3

#define NOTIFY_IDX_ACCEPTED_REJECTED       1
#define NOTIFY_IDX_PUBACK                  2

/*-----------------------------------------------------------*/

typedef enum
{
    ReportStatusNotReceived = 0,
    ReportStatusAccepted = 1,
    ReportStatusRejected = 2,
    ReportStatusInvalid = 3
} ReportStatus_t;

struct MQTTAgentCommandContext
{
    TaskHandle_t xAgentTask;
    size_t uxDeviceIdLen;
    char * pcDeviceId;
    char * pcPublishTopic;
    const char * pcAcceptedTopic;
    const char * pcRejectedTopic;
    uint16_t usPublishTopicLen;
    BaseType_t xWaitingForCallback;
    MQTTAgentHandle_t xAgentHandle;
};

typedef struct MQTTAgentCommandContext DefenderAgentCtx_t;

BaseType_t xExitFlag = pdFALSE;

/*-----------------------------------------------------------*/

/**
 * @brief Subscribe to the device defender topics.
 *
 * @return true if the subscribe is successful;
 * false otherwise.
 */
static bool prvSubscribeToDefenderTopics( DefenderAgentCtx_t * pxCtx );


/**
 */
static void prvPublishOpCb( MQTTAgentCommandContext_t * pxCommandContext,
                            MQTTAgentReturnInfo_t * pxReturnInfo );

/**
 * @brief The callback to execute when there is an incoming publish on the
 * topic for accepted report responses. It verifies the response and sets the
 * report response state accordingly.
 *
 * @param[in] pvIncomingPublishCallbackContext Context of the initial command.
 * @param[in] pxPublishInfo Deserialized publish.
 */
static void prvReportAcceptedCallback( void * pxSubscriptionContext,
                                       MQTTPublishInfo_t * pxPublishInfo );

/**
 * @brief The callback to execute when there is an incoming publish on the
 * topic for rejected report responses. It verifies the response and sets the
 * report response state accordingly.
 *
 * @param[in] pvIncomingPublishCallbackContext Context of the initial command.
 * @param[in] pxPublishInfo Deserialized publish.
 */
static void prvReportRejectedCallback( void * pxSubscriptionContext,
                                       MQTTPublishInfo_t * pxPublishInfo );

/**
 * @brief Collect all the metrics to be sent in the device defender report.
 *
 * @return true if all the metrics are successfully collected;
 * false otherwise.
 */
static CborError prvCollectDeviceMetrics( CborEncoder * pxEncoder );

/**
 * @brief Publish the generated device defender report.
 *
 * @param[in] ulReportLength Length of the device defender report.
 *
 * @return true if the report is published successfully;
 * false otherwise.
 */
static bool prvPublishDeviceMetricsReport( DefenderAgentCtx_t * pxCtx,
                                           uint8_t * pucReportBuf,
                                           uint32_t ulReportLength );

/**
 * @brief Validate the response received from the AWS IoT Device Defender Service.
 *
 * This functions checks that a valid JSON is received and the report ID
 * is same as was sent in the published report.
 *
 * @param[in] pcDefenderResponse The defender response to validate.
 * @param[in] ulDefenderResponseLength Length of the defender response.
 *
 * @return true if the response is valid;
 * false otherwise.
 */
static bool prvValidateDefenderResponse( const char * pcDefenderResponse,
                                         uint32_t ulDefenderResponseLength );

/**
 * @brief The task used to demonstrate the Defender API.
 *
 * This task collects metrics from the device using the functions in
 * metrics_collector.h and uses them to build a defender report using functions
 * in report_builder.h. Metrics include the number for bytes written and read
 * over the network, open TCP and UDP ports, and open TCP sockets. The
 * generated report is then published to the AWS IoT Device Defender service.
 *
 * @param[in] pvParameters Parameters as passed at the time of task creation.
 * Not used in this example.
 */
void vDefenderAgentTask( void * pvParameters );

/*-----------------------------------------------------------*/


static void prvPublishOpCb( MQTTAgentCommandContext_t * pxCommandContext,
                            MQTTAgentReturnInfo_t * pxReturnInfo )
{
    configASSERT_CONTINUE( pxCommandContext );
    configASSERT_CONTINUE( pxReturnInfo );

    if( ( pxCommandContext != NULL ) &&
        ( pxReturnInfo != NULL ) &&
        ( pxCommandContext->xWaitingForCallback == pdTRUE ) )
    {
        ( void ) xTaskNotifyIndexed( pxCommandContext->xAgentTask,
                                     NOTIFY_IDX_PUBACK,
                                     pxReturnInfo->returnCode,
                                     eSetValueWithOverwrite );
    }
}

static void prvClearCtx( DefenderAgentCtx_t * pxCtx )
{
    if( pxCtx->pcDeviceId )
    {
        vPortFree( pxCtx->pcDeviceId );
    }

    if( pxCtx->pcPublishTopic )
    {
        vPortFree( pxCtx->pcPublishTopic );
    }

    memset( pxCtx, 0, sizeof( DefenderAgentCtx_t ) );
}

static bool prvBuildDefenderTopicStrings( DefenderAgentCtx_t * pxCtx )
{
    DefenderStatus_t xRslt = DefenderSuccess;

    configASSERT( pxCtx != NULL );

    pxCtx->pcAcceptedTopic = DEFENDER_API_CBOR_ACCEPTED( "+" );
    pxCtx->pcRejectedTopic = DEFENDER_API_CBOR_REJECTED( "+" );


    pxCtx->usPublishTopicLen = DEFENDER_API_LENGTH_CBOR_PUBLISH( pxCtx->uxDeviceIdLen );

    pxCtx->pcPublishTopic = pvPortMalloc( pxCtx->usPublishTopicLen + 1 );

    if( pxCtx->pcPublishTopic == NULL )
    {
        xRslt = DefenderError;
    }
    else
    {
        uint16_t usLenWritten;

        xRslt = Defender_GetTopic( pxCtx->pcPublishTopic,
                                   pxCtx->usPublishTopicLen,
                                   pxCtx->pcDeviceId,
                                   ( uint16_t ) pxCtx->uxDeviceIdLen,
                                   DefenderCborReportPublish,
                                   &usLenWritten );

        if( usLenWritten != pxCtx->usPublishTopicLen )
        {
            xRslt = DefenderError;
        }
    }

    /* Cleanup on failure */
    if( ( xRslt != DefenderSuccess ) &&
        pxCtx->pcPublishTopic )
    {
        vPortFree( pxCtx->pcPublishTopic );
        pxCtx->pcPublishTopic = NULL;
    }

    return( xRslt == DefenderSuccess );
}

static bool prvSubscribeToDefenderTopics( DefenderAgentCtx_t * pxCtx )
{
    MQTTStatus_t xStatus = MQTTSuccess;

    xStatus = MqttAgent_SubscribeSync( pxCtx->xAgentHandle,
                                       pxCtx->pcAcceptedTopic,
                                       MQTTQoS1,
                                       prvReportAcceptedCallback,
                                       pxCtx );

    configASSERT_CONTINUE( xStatus == MQTTSuccess );

    if( xStatus != MQTTSuccess )
    {
        LogError( "Failed to subscribe to topic: %s", pxCtx->pcAcceptedTopic );
    }
    else
    {
        xStatus = MqttAgent_SubscribeSync( pxCtx->xAgentHandle,
                                           pxCtx->pcRejectedTopic,
                                           MQTTQoS1,
                                           prvReportRejectedCallback,
                                           pxCtx );

        configASSERT_CONTINUE( xStatus == MQTTSuccess );

        if( xStatus != MQTTSuccess )
        {
            LogError( "Failed to subscribe to topic: %s", pxCtx->pcRejectedTopic );
        }
    }

    return( xStatus == MQTTSuccess );
}

static void prvUnsubscribeFromDefenderTopics( DefenderAgentCtx_t * pxCtx )
{
    MQTTStatus_t xStatus = MQTTSuccess;

    xStatus = MqttAgent_UnSubscribeSync( pxCtx->xAgentHandle,
                                         pxCtx->pcAcceptedTopic,
                                         prvReportAcceptedCallback,
                                         pxCtx );

    configASSERT_CONTINUE( xStatus == MQTTSuccess );


    xStatus = MqttAgent_UnSubscribeSync( pxCtx->xAgentHandle,
                                         pxCtx->pcRejectedTopic,
                                         prvReportRejectedCallback,
                                         pxCtx );

    configASSERT_CONTINUE( xStatus == MQTTSuccess );
}

/*-----------------------------------------------------------*/

static void prvPrintHex( const uint8_t * pcPayload,
                         size_t xPayloadLen )
{
#if ( LOG_LEVEL >= LOG_DEBUG )
    for( uint32_t i = 0; i < xPayloadLen; i += 16 )
    {
        LogDebug( "\t%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                  pcPayload[ i + 0 ], pcPayload[ i + 1 ], pcPayload[ i + 2 ], pcPayload[ i + 3 ],
                  pcPayload[ i + 4 ], pcPayload[ i + 5 ], pcPayload[ i + 6 ], pcPayload[ i + 7 ],
                  pcPayload[ i + 8 ], pcPayload[ i + 9 ], pcPayload[ i + 10 ], pcPayload[ i + 11 ],
                  pcPayload[ i + 12 ], pcPayload[ i + 13 ], pcPayload[ i + 14 ], pcPayload[ i + 15 ] );
    }
#else /* LOG_LEVEL >= LOG_DEBUG */
    ( void ) pcPayload;
    ( void ) xPayloadLen;
#endif /* LOG_LEVEL < LOG_DEBUG */
}

/*-----------------------------------------------------------*/

static void prvReportAcceptedCallback( void * pvCtx,
                                       MQTTPublishInfo_t * pxPublishInfo )
{
    DefenderAgentCtx_t * pxCtx = ( DefenderAgentCtx_t * ) pvCtx;

    if( pxCtx->xWaitingForCallback )
    {
        uint32_t ulResponseStatus = ReportStatusNotReceived;

        /* Check if the response is valid and is for the report we
         * published. If so, report was accepted. */
        if( prvValidateDefenderResponse( pxPublishInfo->pPayload,
                                         pxPublishInfo->payloadLength ) )
        {
            ulResponseStatus = ReportStatusAccepted;
        }
        else
        {
            ulResponseStatus = ReportStatusInvalid;
        }

        LogDebug( "Printing returned payload Len: %ld.", pxPublishInfo->payloadLength );

        prvPrintHex( ( uint8_t * ) ( pxPublishInfo->pPayload ), pxPublishInfo->payloadLength );

        /* Send a notification to the task in case it is waiting for this incoming
         * message. */
        ( void ) xTaskNotifyIndexed( pxCtx->xAgentTask,
                                     NOTIFY_IDX_ACCEPTED_REJECTED,
                                     ulResponseStatus,
                                     eSetValueWithOverwrite );
    }
}

/*-----------------------------------------------------------*/



static void prvReportRejectedCallback( void * pvCtx,
                                       MQTTPublishInfo_t * pxPublishInfo )
{
    DefenderAgentCtx_t * pxCtx = ( DefenderAgentCtx_t * ) pvCtx;

    if( pxCtx->xWaitingForCallback )
    {
        uint32_t ulResponseStatus = ReportStatusNotReceived;

        /* Check if the response is valid and is for the report we
         * published. If so, report was accepted. */
        if( prvValidateDefenderResponse( pxPublishInfo->pPayload,
                                         pxPublishInfo->payloadLength ) )
        {
            ulResponseStatus = ReportStatusRejected;
        }
        else
        {
            ulResponseStatus = ReportStatusInvalid;
        }

        LogDebug( "Printing returned payload Len: %ld.", pxPublishInfo->payloadLength );

        prvPrintHex( ( uint8_t * ) ( pxPublishInfo->pPayload ), pxPublishInfo->payloadLength );

        /* Send a notification to the task in case it is waiting for this incoming
         * message. */
        ( void ) xTaskNotifyIndexed( pxCtx->xAgentTask,
                                     NOTIFY_IDX_ACCEPTED_REJECTED,
                                     ulResponseStatus,
                                     eSetValueWithOverwrite );
    }
}

/*-----------------------------------------------------------*/

static CborError prvCollectDeviceMetrics( CborEncoder * pxEncoder )
{
    CborEncoder xMetricsEncoder;
    CborError xError = CborNoError;

    configASSERT( pxEncoder != NULL );

    xError = cbor_encode_text_stringz( pxEncoder, "met" );
    configASSERT_CONTINUE( xError == CborNoError );

    if( xError == CborNoError )
    {
        xError = cbor_encoder_create_map( pxEncoder, &xMetricsEncoder, CborIndefiniteLength );
        configASSERT_CONTINUE( xError == CborNoError );
    }

    if( xError == CborNoError )
    {
        xError = xGetNetworkStats( &xMetricsEncoder );
        configASSERT_CONTINUE( xError == CborNoError );
    }

    if( xError == CborNoError )
    {
        xError = xGetListeningTcpPorts( &xMetricsEncoder );
        configASSERT_CONTINUE( xError == CborNoError );
    }

    if( xError == CborNoError )
    {
        xError = xGetListeningUdpPorts( &xMetricsEncoder );
        configASSERT_CONTINUE( xError == CborNoError );
    }

    if( xError == CborNoError )
    {
        xError = xGetEstablishedConnections( &xMetricsEncoder );
        configASSERT_CONTINUE( xError == CborNoError );
    }

    if( xError == CborNoError )
    {
        xError = cbor_encoder_close_container( pxEncoder, &xMetricsEncoder );
        configASSERT_CONTINUE( xError == CborNoError );
    }

    return xError;
}



/*-----------------------------------------------------------*/

static bool prvPublishDeviceMetricsReport( DefenderAgentCtx_t * pxCtx,
                                           uint8_t * pucReportBuf,
                                           uint32_t ulReportLength )
{
    static MQTTPublishInfo_t xPublishInfo = { 0 };
    MQTTAgentCommandInfo_t xCommandParams = { 0 };
    uint32_t ulStatus = MQTTSuccess;

    xPublishInfo.qos = MQTTQoS1;
    xPublishInfo.pTopicName = pxCtx->pcPublishTopic;
    xPublishInfo.topicNameLength = pxCtx->usPublishTopicLen;
    xPublishInfo.pPayload = pucReportBuf;
    xPublishInfo.payloadLength = ulReportLength;

    xCommandParams.blockTimeMs = MQTT_BLOCK_TIME_MS;
    xCommandParams.cmdCompleteCallback = prvPublishOpCb;
    xCommandParams.pCmdCompleteCallbackContext = pxCtx;

    pxCtx->xWaitingForCallback = pdTRUE;

    ulStatus = MQTTAgent_Publish( pxCtx->xAgentHandle,
                                  &xPublishInfo,
                                  &xCommandParams );

    if( ulStatus == MQTTSuccess )
    {
        ulStatus = MQTTIllegalState;
        BaseType_t xNotifyResult;

        /* Wait for callback */
        xNotifyResult = xTaskNotifyWaitIndexed( NOTIFY_IDX_PUBACK,
                                                0,
                                                0xFFFFFFFF,
                                                &ulStatus,
                                                RESPONSE_TIMEOUT_MS );

        if( xNotifyResult == pdFALSE )
        {
            LogError( "Failed to publish report." );
        }
    }

    LogDebug( "Printing sent payload Len: %ld.", ulReportLength );

    prvPrintHex( pucReportBuf, ulReportLength );

    return( ulStatus == MQTTSuccess );
}

/*-----------------------------------------------------------*/

static bool prvValidateDefenderResponse( const char * pcDefenderResponse,
                                         uint32_t ulDefenderResponseLength )
{
    bool xStatus = true;

    ( void ) pcDefenderResponse;
    ( void ) ulDefenderResponseLength;

    /* TODO: validate that report_id / reportId field matches last sent report */
    return xStatus;
}

/*-----------------------------------------------------------*/

void vDefenderAgentTask( void * pvParameters )
{
    DefenderAgentCtx_t xCtx = { 0 };
    bool xSuccess = false;

    xExitFlag = pdFALSE;

    uint8_t pucReportBuffer[ REPORT_BUFFER_SIZE ] = { 0 };

    /* Remove compiler warnings about unused parameters. */
    ( void ) pvParameters;

    xCtx.pcDeviceId = KVStore_getStringHeap( CS_CORE_THING_NAME, &( xCtx.uxDeviceIdLen ) );
    xCtx.xWaitingForCallback = pdFALSE;
    xCtx.xAgentTask = xTaskGetCurrentTaskHandle();

    xSuccess = ( xCtx.pcDeviceId != NULL &&
                 xCtx.uxDeviceIdLen > 0 &&
                 xCtx.uxDeviceIdLen < UINT16_MAX );

    /* Build strings */
    if( xSuccess )
    {
        xSuccess = prvBuildDefenderTopicStrings( &xCtx );
        configASSERT_CONTINUE( xSuccess );

        if( xSuccess )
        {
            LogDebug( "Built Defender MQTT Topic strings successfully." );
        }
        else
        {
            LogError( "Failed to build MQTT Topic strings." );
        }
    }

    /* Wait for MqttAgent to be ready. */

    vSleepUntilMQTTAgentReady();

    xCtx.xAgentHandle = xGetMqttAgentHandle();

    /* Subscribe to relevant topics */
    if( xSuccess )
    {
        xSuccess = prvSubscribeToDefenderTopics( &xCtx );
        configASSERT_CONTINUE( xSuccess );

        if( xSuccess )
        {
            LogInfo( "Subscribed to defender MQTT topics successfully." );
        }
        else
        {
            LogError( "Failed to subscribe to defender MQTT topics." );
        }
    }

    /* Exit on failure */
    if( xSuccess == false )
    {
        xExitFlag = pdTRUE;
    }

    while( xExitFlag == pdFALSE )
    {
        uint64_t ulReportId = ( uint32_t ) xTaskGetTickCount(); /* TODO: Use a proper timestamp */
        uint32_t ulNotificationValue = 0;
        ReportStatus_t xReportStatus = ReportStatusNotReceived;

        CborEncoder xEncoder;
        CborEncoder xMapEncoder;
        CborEncoder xHeaderEncoder;

        CborError xError = CborNoError;

        cbor_encoder_init( &xEncoder, pucReportBuffer, REPORT_BUFFER_SIZE, 0 );

        xError = cbor_encoder_create_map( &xEncoder, &xMapEncoder, CborIndefiniteLength );
        configASSERT_CONTINUE( xError == CborNoError );

        if( xError == CborNoError )
        {
            xError = cbor_encode_text_stringz( &xMapEncoder, "hed" );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        /* Add Header */
        if( xError == CborNoError )
        {
            xError = cbor_encoder_create_map( &xMapEncoder, &xHeaderEncoder, 2 );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        /* Report ID */
        if( xError == CborNoError )
        {
            xError = cbor_encode_text_stringz( &xHeaderEncoder, "rid" );
            configASSERT_CONTINUE( xError == CborNoError );

            xError |= cbor_encode_uint( &xHeaderEncoder, ulReportId );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        /* Version */
        if( xError == CborNoError )
        {
            xError = cbor_encode_text_stringz( &xHeaderEncoder, "v" );
            configASSERT_CONTINUE( xError == CborNoError );

            xError = cbor_encode_text_stringz( &xHeaderEncoder, "1.0" );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( &xMapEncoder, &xHeaderEncoder );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            /* Collect device metrics. */
            LogInfo( "Collecting device metrics..." );
            xError = prvCollectDeviceMetrics( &xMapEncoder );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( &xEncoder, &xMapEncoder );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError != CborNoError )
        {
            LogError( "Failed to collect device metrics." );
        }

        /* Format defined here:
         * https://docs.aws.amazon.com/iot/latest/developerguide/detect-device-side-metrics.html
         */
        if( xError == CborNoError )
        {
            size_t xLen = cbor_encoder_get_buffer_size( &xEncoder, pucReportBuffer );
            LogInfo( "Publishing defender metrics report." );

            xSuccess = prvPublishDeviceMetricsReport( &xCtx, pucReportBuffer, xLen );

            if( xSuccess != true )
            {
                LogError( "Failed to publish device defender report." );
            }
        }

        /* Wait for the response to our report */
        if( xTaskNotifyWaitIndexed( NOTIFY_IDX_ACCEPTED_REJECTED,
                                    0,
                                    0xFFFFFFFF,
                                    &ulNotificationValue,
                                    pdMS_TO_TICKS( RESPONSE_TIMEOUT_MS ) ) == pdTRUE )
        {
            xReportStatus = ulNotificationValue;
        }

        xCtx.xWaitingForCallback = pdFALSE;

        switch( xReportStatus )
        {
            case ReportStatusAccepted:
                LogInfo( "Defender report accepted." );
                break;

            case ReportStatusRejected:
                LogError( "Defender report rejected." );
                break;

            case ReportStatusInvalid:
            case ReportStatusNotReceived:
            default:
                LogError( "Defender report response not received." );
                break;
        }

        LogDebug( "Sleeping until next report." );
        vTaskDelay( pdMS_TO_TICKS( MS_BETWEEN_REPORTS ) );
    }

    LogSys( "Exiting..." );

    prvUnsubscribeFromDefenderTopics( &xCtx );

    prvClearCtx( &xCtx );


    vTaskDelete( NULL );
}

/*-----------------------------------------------------------*/
