
/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "MQTTFileDownloader.h"
#include "jobs.h"
#include "mqtt_wrapper.h"
#include "ota_demo.h"
#include "ota_job_processor.h"
#include "os/ota_os_freertos.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "sys_evt.h"

#define CONFIG_MAX_FILE_SIZE    65536U
#define NUM_OF_BLOCKS_REQUESTED 1U
#define START_JOB_MSG_LENGTH    147U
#define MAX_THING_NAME_SIZE     128U
#define MAX_JOB_ID_LENGTH       64U
#define UPDATE_JOB_MSG_LENGTH   48U
#define MAX_NUM_OF_OTA_DATA_BUFFERS 5U

MqttFileDownloaderContext_t mqttFileDownloaderContext = { 0 };
static uint32_t numOfBlocksRemaining = 0;
static uint32_t currentBlockOffset = 0;
static uint8_t currentFileId = 0;
static uint32_t totalBytesReceived = 0;
static uint8_t downloadedData[ CONFIG_MAX_FILE_SIZE ] = { 0 };
char globalJobId[ MAX_JOB_ID_LENGTH ] = { 0 };
extern EventGroupHandle_t xSystemEvents;

static OtaDataEvent_t dataBuffers[MAX_NUM_OF_OTA_DATA_BUFFERS] = { 0 };
static OtaJobEventData_t jobDocBuffer = { 0 };
static SemaphoreHandle_t bufferSemaphore;

static OtaState_t otaAgentState = OtaAgentStateInit;

static void finishDownload( void );

static void processOTAEvents( void );

static void requestJobDocumentHandler( void );

static bool receivedJobDocumentHandler( OtaJobEventData_t * jobDoc );

static bool jobDocumentParser( char * message, size_t messageLength, AfrOtaJobDocumentFields_t *jobFields );

static void initMqttDownloader( AfrOtaJobDocumentFields_t *jobFields );

static OtaDataEvent_t * getOtaDataEventBuffer( void );

static void freeOtaDataEventBuffer( OtaDataEvent_t * const buffer );

static void handleMqttStreamsBlockArrived( uint8_t *data, size_t dataLength );

static void requestDataBlock( void );



static void requestDataBlock( void )
{
    char getStreamRequest[ GET_STREAM_REQUEST_BUFFER_SIZE ];
    size_t getStreamRequestLength = 0U;

    /*
     * MQTT streams Library:
     * Creating the Get data block request. MQTT streams library only
     * creates the get block request. To publish the request, MQTT libraries
     * like coreMQTT are required.
     */
    getStreamRequestLength = mqttDownloader_createGetDataBlockRequest( mqttFileDownloaderContext.dataType,
                                        currentFileId,
                                        mqttFileDownloader_CONFIG_BLOCK_SIZE,
                                        currentBlockOffset,
                                        NUM_OF_BLOCKS_REQUESTED,
                                        getStreamRequest,
                                        GET_STREAM_REQUEST_BUFFER_SIZE );

    mqttWrapper_publish( mqttFileDownloaderContext.topicGetStream,
                         mqttFileDownloaderContext.topicGetStreamLength,
                         ( uint8_t * ) getStreamRequest,
                         getStreamRequestLength );
}

/*-----------------------------------------------------------*/


/* AFR OTA library callback */
static void processJobFile( AfrOtaJobDocumentFields_t * params )
{
    char thingName[ MAX_THING_NAME_SIZE + 1 ] = { 0 };
    size_t thingNameLength = 0U;

    mqttWrapper_getThingName( thingName, &thingNameLength );

    numOfBlocksRemaining = params->fileSize /
                           mqttFileDownloader_CONFIG_BLOCK_SIZE;
    numOfBlocksRemaining += ( params->fileSize %
                                  mqttFileDownloader_CONFIG_BLOCK_SIZE >
                              0 )
                                ? 1
                                : 0;
    currentFileId = params->fileId;
    currentBlockOffset = 0;
    totalBytesReceived = 0;
    /*
     * MQTT streams Library:
     * Initializing the MQTT streams downloader. Passing the
     * parameters extracted from the AWS IoT OTA jobs document
     * using OTA jobs parser.
     */
    mqttDownloader_init( &mqttFileDownloaderContext,
                         params->imageRef,
                         params->imageRefLen,
                         thingName,
                         thingNameLength,
                         DATA_TYPE_CBOR );

    mqttWrapper_subscribe( mqttFileDownloaderContext.topicStreamData,
                            mqttFileDownloaderContext.topicStreamDataLength );

    LogInfo("Starting The Download. \n");
    /* Request the first block */
    requestDataBlock();
}

void otaAgentTask( void * parameters )
{
	( void ) parameters;

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

    otaDemo_start();

    for( ;; )
    {
        vTaskDelay( portMAX_DELAY );
    }
}

void otaDemo_start( void )
{
    OtaEventMsg_t initEvent = { 0 };

    if( !mqttWrapper_isConnected() )
    {
    	LogInfo("MQTT not connected, exiting!");
        return;
    }

    bufferSemaphore = xSemaphoreCreateMutex();

    if( bufferSemaphore != NULL )
    {
        memset( dataBuffers, 0x00, sizeof( dataBuffers ) );
    }

    LogError("Starting OTA thread.");

    OtaInitEvent_FreeRTOS();

    initEvent.eventId = OtaAgentEventRequestJobDocument;
    OtaSendEvent_FreeRTOS( &initEvent );

    while (otaAgentState != OtaAgentStateStopped ) {
        processOTAEvents();
    }
}

OtaState_t getOtaAgentState()
{
    return otaAgentState;
}

static void requestJobDocumentHandler()
{
    char thingName[ MAX_THING_NAME_SIZE + 1 ] = { 0 };
    size_t thingNameLength = 0U;
    char topicBuffer[ TOPIC_BUFFER_SIZE + 1 ] = { 0 };
    char messageBuffer[ START_JOB_MSG_LENGTH ] = { 0 };
    size_t topicLength = 0U;
    mqttWrapper_getThingName( thingName, &thingNameLength );

    /*
     * AWS IoT Jobs library:
     * Creates the topic string for a StartNextPendingJobExecution request.
     * It used to check if any pending jobs are available.
     */
    Jobs_StartNext(topicBuffer,
                   TOPIC_BUFFER_SIZE,
                   thingName,
                   thingNameLength,
                   &topicLength);

    /*
     * AWS IoT Jobs library:
     * Creates the message string for a StartNextPendingJobExecution request.
     * It will be sent on the topic created in the previous step.
     */
    size_t messageLength = Jobs_StartNextMsg("test",
                                             4U,
                                             messageBuffer,
                                             START_JOB_MSG_LENGTH );

    mqttWrapper_publish(topicBuffer,
                        topicLength,
                        ( uint8_t * ) messageBuffer,
                        messageLength);

}

static void initMqttDownloader( AfrOtaJobDocumentFields_t *jobFields )
{
    char thingName[ MAX_THING_NAME_SIZE + 1 ] = { 0 };
    size_t thingNameLength = 0U;

    numOfBlocksRemaining = jobFields->fileSize /
                            mqttFileDownloader_CONFIG_BLOCK_SIZE;
    numOfBlocksRemaining += ( jobFields->fileSize %
                            mqttFileDownloader_CONFIG_BLOCK_SIZE > 0 ) ? 1 : 0;
    currentFileId = jobFields->fileId;
    currentBlockOffset = 0;
    totalBytesReceived = 0;

    mqttWrapper_getThingName( thingName, &thingNameLength );

    /*
     * MQTT streams Library:
     * Initializing the MQTT streams downloader. Passing the
     * parameters extracted from the AWS IoT OTA jobs document
     * using OTA jobs parser.
     */
    mqttDownloader_init( &mqttFileDownloaderContext,
                        jobFields->imageRef,
                        jobFields->imageRefLen,
                        thingName,
                        thingNameLength,
                        DATA_TYPE_JSON );

    mqttWrapper_subscribe( mqttFileDownloaderContext.topicStreamData,
                           mqttFileDownloaderContext.topicStreamDataLength );
}

static bool receivedJobDocumentHandler( OtaJobEventData_t * jobDoc )
{
    bool parseJobDocument = false;
    bool handled = false;
    char * jobId;
    size_t jobIdLength = 0U;
    AfrOtaJobDocumentFields_t jobFields = { 0 };

    /*
     * AWS IoT Jobs library:
     * Extracting the job ID from the received OTA job document.
     */
    jobIdLength = Jobs_GetJobId( (char *)jobDoc->jobData, jobDoc->jobDataLength, &jobId );

    if ( jobIdLength )
    {
        if ( strncmp( globalJobId, jobId, jobIdLength ) )
        {
            parseJobDocument = true;
            strncpy( globalJobId, jobId, jobIdLength );
        }
        else
        {
            handled = true;
        }
    }

    if ( parseJobDocument )
    {
        handled = jobDocumentParser( (char * )jobDoc->jobData, jobDoc->jobDataLength, &jobFields );
        if (handled)
        {
            initMqttDownloader( &jobFields );
        }
    }

    return handled;
}

/*-----------------------------------------------------------*/

static bool jobHandlerChain( char * message, size_t messageLength )
{
    char * jobDoc;
    size_t jobDocLength = 0U;
    char * jobId;
    size_t jobIdLength = 0U;
    int8_t fileIndex = 0;

    /*
     * AWS IoT Jobs library:
     * Extracting the OTA job document from the jobs message recevied from AWS IoT core.
     */
    jobDocLength = Jobs_GetJobDocument( message, messageLength, &jobDoc );

    /*
     * AWS IoT Jobs library:
     * Extracting the job ID from the received OTA job document.
     */
    jobIdLength = Jobs_GetJobId( message, messageLength, &jobId );

    if( globalJobId[ 0 ] == 0 )
    {
        strncpy( globalJobId, jobId, jobIdLength );
    }

    if( jobDocLength != 0U && jobIdLength != 0U )
    {
        AfrOtaJobDocumentFields_t jobFields = { 0 };

        do
        {
            /*
             * AWS IoT Jobs library:
             * Parsing the OTA job document to extract all of the parameters needed to download
             * the new firmware.
             */
            fileIndex = otaParser_parseJobDocFile( jobDoc,
                                                   jobDocLength,
                                                   fileIndex,
                                                   &jobFields );

            if( fileIndex >= 0 )
            {
                LogInfo("Received OTA Job \n");
                processJobFile( &jobFields );
            }
        } while( fileIndex > 0 );
    }

    // File index will be -1 if an error occured, and 0 if all files were
    // processed
    return fileIndex == 0;
}

/*-----------------------------------------------------------*/

static void processOTAEvents() {
    OtaEventMsg_t recvEvent = { 0 };
    OtaEvent_t recvEventId = 0;
    OtaEventMsg_t nextEvent = { 0 };

    OtaReceiveEvent_FreeRTOS(&recvEvent);
    recvEventId = recvEvent.eventId;
    LogInfo("Received Event is %d \n", recvEventId);

    switch (recvEventId)
    {
    case OtaAgentEventRequestJobDocument:
        LogInfo("Request Job Document event Received \n");
        LogInfo("-------------------------------------\n");
        requestJobDocumentHandler();
        otaAgentState = OtaAgentStateRequestingJob;
        break;

//    case OtaAgentEventJobAccepted:
//    	LogInfo("Job Accepted event Received \n");
//    	LogInfo("-------------------------------------\n");
//    	jobHandlerChain( ( char * ) recvEvent.jobEvent->jobData,
//    			         recvEvent.jobEvent->jobDataLength );
//
//    	vPortFree( recvEvent.jobEvent );
//    	break;

    case OtaAgentEventReceivedJobDocument:
        LogInfo("Received Job Document event Received \n");
        LogInfo("-------------------------------------\n");

        if (otaAgentState == OtaAgentStateSuspended)
        {
            LogInfo("OTA-Agent is in Suspend State. Hence dropping Job Document. \n");
            break;
        }

        if ( receivedJobDocumentHandler(recvEvent.jobEvent) )
        {
            LogInfo( "Received OTA Job. \n" );
            nextEvent.eventId = OtaAgentEventRequestFileBlock;
            OtaSendEvent_FreeRTOS( &nextEvent );
        }
        else
        {
            LogInfo("This is not an OTA job \n");
        }
        otaAgentState = OtaAgentStateCreatingFile;
        break;

    case OtaAgentEventRequestFileBlock:
        otaAgentState = OtaAgentStateRequestingFileBlock;
        LogInfo("Request File Block event Received \n");
        LogInfo("-----------------------------------\n");
        if (currentBlockOffset == 0)
        {
            LogInfo( "Starting The Download. \n" );
        }
        requestDataBlock();
        break;

    case OtaAgentEventReceivedFileBlock:
        LogInfo("Received File Block event Received \n");
        LogInfo("---------------------------------------\n");
        if (otaAgentState == OtaAgentStateSuspended)
        {
            LogInfo("OTA-Agent is in Suspend State. Hence dropping File Block. \n");
            freeOtaDataEventBuffer(recvEvent.dataEvent);
            break;
        }
        uint8_t decodedData[ mqttFileDownloader_CONFIG_BLOCK_SIZE ];
        size_t decodedDataLength = 0;
        /*
         * MQTT streams Library:
         * Extracting and decoding the received data block from the incoming MQTT message.
         */
        mqttDownloader_processReceivedDataBlock(
            &mqttFileDownloaderContext,
            recvEvent.dataEvent->data,
            recvEvent.dataEvent->dataLength,
            decodedData,
            &decodedDataLength );
        handleMqttStreamsBlockArrived(decodedData, decodedDataLength);
        freeOtaDataEventBuffer(recvEvent.dataEvent);
        numOfBlocksRemaining--;
        currentBlockOffset++;

        if( numOfBlocksRemaining == 0 )
        {
            nextEvent.eventId = OtaAgentEventCloseFile;
            OtaSendEvent_FreeRTOS( &nextEvent );
        }
        else
        {
            nextEvent.eventId = OtaAgentEventRequestFileBlock;
            OtaSendEvent_FreeRTOS( &nextEvent );
        }
        break;

    case OtaAgentEventCloseFile:
        LogInfo("Close file event Received \n");
        LogInfo("-----------------------\n");
        LogInfo( "Downloaded Data %s \n", ( char * ) downloadedData );
        finishDownload();
        otaAgentState = OtaAgentStateStopped;
        break;

    case OtaAgentEventSuspend:
        LogInfo("Suspend Event Received \n");
        LogInfo("-----------------------\n");
        otaAgentState = OtaAgentStateSuspended;
        break;

    case OtaAgentEventResume:
        LogInfo("Resume Event Received \n");
        LogInfo("---------------------\n");
        otaAgentState = OtaAgentStateRequestingJob;
        nextEvent.eventId = OtaAgentEventRequestJobDocument;
        OtaSendEvent_FreeRTOS( &nextEvent );

    default:
        break;
    }
}

/* Implemented for use by the MQTT library */
bool otaDemo_handleIncomingMQTTMessage( char * topic,
                                        size_t topicLength,
                                        uint8_t * message,
                                        size_t messageLength )
{
    OtaEventMsg_t nextEvent = { 0 };
    char thingName[ MAX_THING_NAME_SIZE + 1 ] = { 0 };
    size_t thingNameLength = 0U;



    /*
	 * MQTT streams Library:
	 * Checks if the incoming message contains the requested data block. It is performed by
	 * comparing the incoming MQTT message topic with MQTT streams topics.
	 */
    bool handled = mqttDownloader_isDataBlockReceived(&mqttFileDownloaderContext, topic, topicLength);
	if (handled)
	{
		nextEvent.eventId = OtaAgentEventReceivedFileBlock;
		OtaDataEvent_t * dataBuf = getOtaDataEventBuffer();
		memcpy(dataBuf->data, message, messageLength);
		nextEvent.dataEvent = dataBuf;
		dataBuf->dataLength = messageLength;
		OtaSendEvent_FreeRTOS( &nextEvent );
	}
	else
	{
		mqttWrapper_getThingName(thingName, &thingNameLength);
		/*
		 * AWS IoT Jobs library:
		 * Checks if a message comes from the start-next/accepted reserved topic.
		 */
		 handled = Jobs_IsStartNextAccepted( topic,
												 topicLength,
												 thingName,
												 thingNameLength );

		if( handled )
		{
			memcpy(jobDocBuffer.jobData, message, messageLength);
			nextEvent.jobEvent = &jobDocBuffer;
			jobDocBuffer.jobDataLength = messageLength;
			nextEvent.eventId = OtaAgentEventReceivedJobDocument;
			OtaSendEvent_FreeRTOS( &nextEvent );
		}
	}
//
//    if( !handled )
//    {
//        LogInfo( "Unrecognized incoming MQTT message received on topic: "
//                "%.*s\nMessage: %.*s\n",
//                ( unsigned int ) topicLength,
//                topic,
//                ( unsigned int ) messageLength,
//                ( char * ) message );
//    }
    return handled;
}

static bool jobDocumentParser( char * message, size_t messageLength, AfrOtaJobDocumentFields_t *jobFields )
{
    char * jobDoc;
    size_t jobDocLength = 0U;
    int8_t fileIndex = 0;

    /*
     * AWS IoT Jobs library:
     * Extracting the OTA job document from the jobs message recevied from AWS IoT core.
     */
    jobDocLength = Jobs_GetJobDocument( message, messageLength, &jobDoc );

    if( jobDocLength != 0U )
    {
        do
        {
            /*
             * AWS IoT Jobs library:
             * Parsing the OTA job document to extract all of the parameters needed to download
             * the new firmware.
             */
            fileIndex = otaParser_parseJobDocFile( jobDoc,
                                                jobDocLength,
                                                fileIndex,
                                                jobFields );
        } while( fileIndex > 0 );
    }

    // File index will be -1 if an error occured, and 0 if all files were
    // processed
    return fileIndex == 0;
}

/* Stores the received data blocks in the flash partition reserved for OTA */
static void handleMqttStreamsBlockArrived(
    uint8_t *data, size_t dataLength )
{
    assert( ( totalBytesReceived + dataLength ) <
            CONFIG_MAX_FILE_SIZE );

    LogInfo( "Downloaded block %u of %u. \n", currentBlockOffset, (currentBlockOffset + numOfBlocksRemaining) );

    memcpy( downloadedData + totalBytesReceived,
            data,
            dataLength );

    totalBytesReceived += dataLength;

}

static void finishDownload()
{
    /* TODO: Do something with the completed download */
    /* Start the bootloader */
    char thingName[ MAX_THING_NAME_SIZE + 1 ] = { 0 };
    size_t thingNameLength = 0U;
    char topicBuffer[ TOPIC_BUFFER_SIZE + 1 ] = { 0 };
    size_t topicBufferLength = 0U;
    char messageBuffer[ UPDATE_JOB_MSG_LENGTH ] = { 0 };

    mqttWrapper_getThingName( thingName, &thingNameLength );

    /*
     * AWS IoT Jobs library:
     * Creating the MQTT topic to update the status of OTA job.
     */
    Jobs_Update(topicBuffer,
                TOPIC_BUFFER_SIZE,
                thingName,
                thingNameLength,
                globalJobId,
                strnlen( globalJobId, 1000U ),
                &topicBufferLength);

    /*
     * AWS IoT Jobs library:
     * Creating the message which contains the status of OTA job.
     * It will be published on the topic created in the previous step.
     */
    size_t messageBufferLength = Jobs_UpdateMsg(Succeeded,
                                                "2",
                                                1U,
                                                messageBuffer,
                                                UPDATE_JOB_MSG_LENGTH );

    mqttWrapper_publish(topicBuffer,
                        topicBufferLength,
                        ( uint8_t * ) messageBuffer,
                        messageBufferLength);
    LogInfo( "\033[1;32mOTA Completed successfully!\033[0m\n" );
    globalJobId[ 0 ] = 0U;
}


static void freeOtaDataEventBuffer( OtaDataEvent_t * const pxBuffer )
{
    if( xSemaphoreTake( bufferSemaphore, portMAX_DELAY ) == pdTRUE )
    {
        pxBuffer->bufferUsed = false;
        ( void ) xSemaphoreGive( bufferSemaphore );
    }
    else
    {
        LogInfo( "Failed to get buffer semaphore.\n" );
    }
}

static OtaDataEvent_t * getOtaDataEventBuffer( void )
{
    uint32_t ulIndex = 0;
    OtaDataEvent_t * freeBuffer = NULL;

    if( xSemaphoreTake( bufferSemaphore, portMAX_DELAY ) == pdTRUE )
    {
        for( ulIndex = 0; ulIndex < MAX_NUM_OF_OTA_DATA_BUFFERS; ulIndex++ )
        {
            if( dataBuffers[ ulIndex ].bufferUsed == false )
            {
                dataBuffers[ ulIndex ].bufferUsed = true;
                freeBuffer = &dataBuffers[ ulIndex ];
                break;
            }
        }

        ( void ) xSemaphoreGive( bufferSemaphore );
    }
    else
    {
        LogInfo("Failed to get buffer semaphore. \n" );
    }

    return freeBuffer;
}

#if 0


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
 */

/**
 * @file ota_over_mqtt_demo.c
 * @brief Over The Air Update demo using coreMQTT Agent.
 *
 * The file demonstrates how to perform Over The Air update using OTA agent and coreMQTT
 * library. It creates an OTA agent task which manages the OTA firmware update
 * for the device. The example also provides implementations to subscribe, publish,
 * and receive data from an MQTT broker. The implementation uses coreMQTT agent which manages
 * thread safety of the MQTT operations and allows OTA agent to share the same MQTT
 * broker connection with other tasks. OTA agent invokes the callback implementations to
 * publish job related control information, as well as receive chunks
 * of presigned firmware image from the MQTT broker.
 *
 * See https://freertos.org/mqtt/mqtt-agent-demo.html
 * See https://freertos.org/ota/ota-mqtt-agent-demo.html
 */

#include "logging_levels.h"
/* define LOG_LEVEL here if you want to modify the logging level from the default */

#define LOG_LEVEL    LOG_INFO

#include "logging.h"

/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "sys_evt.h"

#include "ota_config.h"

/* MQTT library includes. */
#include "core_mqtt_agent.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

/* OTA Library include. */
#include "ota.h"

/* OTA Library Interface include. */
#include "ota_os_freertos.h"
#include "ota_mqtt_interface.h"

/* Include firmware version struct definition. */
#include "ota_appversion32.h"

/* Include platform abstraction header. */
#include "ota_pal.h"

#include "mqtt_agent_task.h"

#include "kvstore.h"

#ifdef TFM_PSA_API
    #include "tfm_fwu_defs.h"
    #include "psa/update.h"
#endif


/*------------- Demo configurations -------------------------*/

/**
 * @brief The maximum size of the file paths used in the demo.
 */
#define otaexampleMAX_FILE_PATH_SIZE              ( 260 )

/**
 * @brief The maximum size of the stream name required for downloading update file
 * from streaming service.
 */
#define otaexampleMAX_STREAM_NAME_SIZE            ( 128 )

/**
 * @brief The delay used in the OTA demo task to periodically output the OTA
 * statistics like number of packets received, dropped, processed and queued per connection.
 */
#define otaexampleTASK_DELAY_MS                   ( 30 * 1000U )

/**
 * @brief The timeout for an OTA job in the OTA demo task
 */
#define otaexampleOTA_UPDATE_TIMEOUT_MS           ( 120 * 1000U )

/**
 * @brief The maximum time for which OTA demo waits for an MQTT operation to be complete.
 * This involves receiving an acknowledgment for broker for SUBSCRIBE, UNSUBSCRIBE and non
 * QOS0 publishes.
 */
#define otaexampleMQTT_TIMEOUT_MS                 ( 10 * 1000U )

/**
 * @brief The common prefix for all OTA topics.
 *
 * Thing name is substituted with a wildcard symbol `+`. OTA agent
 * registers with MQTT broker with the thing name in the topic. This topic
 * filter is used to match incoming packet received and route them to OTA.
 * Thing name is not needed for this matching.
 */
#define OTA_TOPIC_PREFIX                          "$aws/things"

/**
 * @brief Length of OTA topics prefix.
 */
#define OTA_PREFIX_LENGTH                         ( sizeof( OTA_TOPIC_PREFIX ) - 1UL )

/**
 * @brief Wildcard topic filter for job notification.
 * The filter is used to match the constructed job notify topic filter from OTA agent and register
 * appropriate callback for it.
 */
#define OTA_JOB_NOTIFY_TOPIC_FILTER               OTA_TOPIC_PREFIX "/+/jobs/notify-next"

/**
 * @brief Length of job notification topic filter.
 */
#define OTA_JOB_NOTIFY_TOPIC_FILTER_LENGTH        ( ( uint16_t ) ( sizeof( OTA_JOB_NOTIFY_TOPIC_FILTER ) - 1UL ) )

/**
 * @brief Wildcard topic filter for matching job response messages.
 * This topic filter is used to match the responses from OTA service for OTA agent job requests. THe
 * topic filter is a reserved topic which is not subscribed with MQTT broker.
 */
#define OTA_JOB_ACCEPTED_RESPONSE_TOPIC_FILTER    OTA_TOPIC_PREFIX "/+/jobs/$next/get/#"

/**
 * @brief Wildcard topic filter for matching OTA job update messages (used to detect cancellation).
 */
#define OTA_JOB_UPDATE_TOPIC_FILTER               OTA_TOPIC_PREFIX "/+/jobs/+/update/rejected"

#define OTA_JOB_TOPIC_FILTER                      OTA_TOPIC_PREFIX "/+/jobs/#"
#define OTA_JOB_TOPIC_FILTER_LEN                  ( sizeof( OTA_TOPIC_PREFIX "/+/jobs/#" ) - 1 )

/**
 * @brief Wildcard topic filter for matching OTA data packets.
 *  The filter is used to match the constructed data stream topic filter from OTA agent and register
 * appropriate callback for it.
 */
#define OTA_DATA_STREAM_TOPIC_FILTER              OTA_TOPIC_PREFIX "/+/streams/#"

/**
 * @brief Length of data stream topic filter.
 */
#define OTA_DATA_STREAM_TOPIC_FILTER_LENGTH       ( ( uint16_t ) ( sizeof( OTA_DATA_STREAM_TOPIC_FILTER ) - 1 ) )


/**
 * @brief Starting index of client identifier within OTA topic.
 */
#define OTA_TOPIC_CLIENT_IDENTIFIER_START_IDX    ( OTA_PREFIX_LENGTH + 1 )

/**
 * @brief Used to clear bits in a task's notification value.
 */
#define otaexampleMAX_UINT32                     ( 0xffffffff )

/**
 * @brief Task priority of OTA agent.
 */
#define otaexampleAGENT_TASK_PRIORITY            ( tskIDLE_PRIORITY + 3 )

/**
 * @brief Maximum stack size of OTA agent task.
 */
#define otaexampleAGENT_TASK_STACK_SIZE          ( 4096 )

static const char * pOtaAgentStateStrings[ OtaAgentStateAll + 1 ] =
{
    "Init",
    "Ready",
    "RequestingJob",
    "WaitingForJob",
    "CreatingFile",
    "RequestingFileBlock",
    "WaitingForFileBlock",
    "ClosingFile",
    "Suspended",
    "ShuttingDown",
    "Stopped",
    "All"
};

/**
 * @brief A statically allocated array of event buffers used by the OTA agent.
 * Maximum number of buffers are determined by how many chunks are requested
 * by OTA agent at a time along with an extra buffer to handle control message.
 * The size of each buffer is determined by the maximum size of firmware image
 * chunk, and other metadata send along with the chunk.
 */
typedef struct OtaEventBufferPool
{
    OtaEventData_t eventBuffer[ otaconfigMAX_NUM_OTA_DATA_BUFFERS ];
    SemaphoreHandle_t lock;
} OtaEventBufferPool_t;

/**
 * @brief The structure wraps the static buffers allocated by an OTA application
 * and used by OTA Agent. Static buffer should be in scope as long as the OTA Agent
 * task is active.
 */
typedef struct OtaAppStaticBuffer
{
    /**
     * @brief Buffer used to store the firmware image file path.
     * Buffer is passed to the OTA agent during initialization.
     */
    uint8_t updateFilePath[ otaexampleMAX_FILE_PATH_SIZE ];

    /**
     * @brief Buffer used to store the code signing certificate file path.
     * Buffer is passed to the OTA agent during initialization.
     */
    uint8_t certFilePath[ otaexampleMAX_FILE_PATH_SIZE ];

    /**
     * @brief Buffer used to store the name of the data stream.
     * Buffer is passed to the OTA agent during initialization.
     */
    uint8_t streamName[ otaexampleMAX_STREAM_NAME_SIZE ];

    /**
     * @brief Buffer used decode the CBOR message from the MQTT payload.
     * Buffer is passed to the OTA agent during initialization.
     */
    uint8_t decodeMem[ ( 1U << otaconfigLOG2_FILE_BLOCK_SIZE ) ];

    /**
     * @brief Application buffer used to store the bitmap for requesting firmware image
     * chunks from MQTT broker. Buffer is passed to the OTA agent during initialization.
     */
    uint8_t bitmap[ OTA_MAX_BLOCK_BITMAP_SIZE ];

    OtaEventBufferPool_t eventBufferPool;
} OtaAppStaticBuffer_t;

/**
 * @brief Defines the structure to use as the command callback context in this
 * demo.
 */
struct MQTTAgentCommandContext
{
    TaskHandle_t xTaskToNotify;
};

/**
 * @brief Function used by OTA agent to publish control messages to the MQTT broker.
 *
 * The implementation uses MQTT agent to queue a publish request. It then waits
 * for the request complete notification from the agent. The notification along with result of the
 * operation is sent back to the caller task using xTaskNotify API. For publishes involving QOS 1 and
 * QOS2 the operation is complete once an acknowledgment (PUBACK) is received. OTA agent uses this function
 * to fetch new job, provide status update and send other control related messages to the MQTT broker.
 *
 * @param[in] pacTopic Topic to publish the control packet to.
 * @param[in] topicLen Length of the topic string.
 * @param[in] pMsg Message to publish.
 * @param[in] msgSize Size of the message to publish.
 * @param[in] qos Qos for the publish.
 * @return OtaMqttSuccess if successful. Appropriate error code otherwise.
 */
static OtaMqttStatus_t prvMQTTPublish( const char * const pacTopic,
                                       uint16_t topicLen,
                                       const char * pMsg,
                                       uint32_t msgSize,
                                       uint8_t qos );

/**
 * @brief Function used by OTA agent to subscribe for a control or data packet from the MQTT broker.
 *
 * The implementation queues a SUBSCRIBE request for the topic filter with the MQTT agent. It then waits for
 * a notification of the request completion. Notification will be sent back to caller task,
 * using xTaskNotify APIs. MQTT agent also stores a callback provided by this function with
 * the associated topic filter. The callback will be used to
 * route any data received on the matching topic to the OTA agent. OTA agent uses this function
 * to subscribe to all topic filters necessary for receiving job related control messages as
 * well as firmware image chunks from MQTT broker.
 *
 * @param[in] pTopicFilter The topic filter used to subscribe for packets.
 * @param[in] topicFilterLength Length of the topic filter string.
 * @param[in] ucQoS Intended qos value for the messages received on this topic.
 * @return OtaMqttSuccess if successful. Appropriate error code otherwise.
 */
static OtaMqttStatus_t prvMQTTSubscribe( const char * pTopicFilter,
                                         uint16_t topicFilterLength,
                                         uint8_t ucQoS );

/**
 * @brief Function is used by OTA agent to unsubscribe a topicfilter from MQTT broker.
 *
 * The implementation queues an UNSUBSCRIBE request for the topic filter with the MQTT agent. It then waits
 * for a successful completion of the request from the agent. Notification along with results of
 * operation is sent using xTaskNotify API to the caller task. MQTT agent also removes the topic filter
 * subscription from its memory so any future
 * packets on this topic will not be routed to the OTA agent.
 *
 * @param[in] pTopicFilter Topic filter to be unsubscribed.
 * @param[in] topicFilterLength Length of the topic filter.
 * @param[in] ucQos Qos value for the topic.
 * @return OtaMqttSuccess if successful. Appropriate error code otherwise.
 *
 */
static OtaMqttStatus_t prvMQTTUnsubscribe( const char * pTopicFilter,
                                           uint16_t topicFilterLength,
                                           uint8_t ucQoS );

/**
 * @brief Initialize the OTA event buffer pool.
 *
 * @param[in] pxEventBufferPool Pointer to the event buffer pool to be initialized.
 * @return pdTRUE if Event Buffer pool is initialized.
 */
static BaseType_t prvOTAEventBufferPoolInit( OtaEventBufferPool_t * pxBufferPool );

/**
 * @brief Fetch an unused OTA event buffer from the pool.
 *
 * Demo uses a simple statically allocated array of fixed size event buffers. The
 * number of event buffers is configured by the param otaconfigMAX_NUM_OTA_DATA_BUFFERS
 * within ota_config.h. This function is used to fetch a free buffer from the pool for processing
 * by the OTA agent task. It uses a mutex for thread safe access to the pool.
 *
 * @param[in] pxEventBufferPool Pointer to the Event Buffer pool.
 * @return A pointer to an unused buffer from the pool. NULL if there are no buffers available.
 */
static OtaEventData_t * prvOTAEventBufferGet( OtaEventBufferPool_t * pxBufferPool );

/**
 * @brief Free an event buffer back to pool
 *
 * OTA demo uses a statically allocated array of fixed size event buffers . The
 * number of event buffers is configured by the param otaconfigMAX_NUM_OTA_DATA_BUFFERS
 * within ota_config.h. The function is used by the OTA application callback to free a buffer,
 * after OTA agent has completed processing with the event. The access to the pool is made thread safe
 * using a mutex.
 *
 * @param[in] pxEventBufferPool Pointer to the Event Buffer pool.
 * @param[in] pxBuffer Pointer to the buffer to be freed.
 */
static void prvOTAEventBufferFree( OtaEventBufferPool_t * pxBufferPool,
                                   OtaEventData_t * const pxBuffer );

/**
 * @brief The function which runs the OTA agent task.
 *
 * The function runs the OTA Agent Event processing loop, which waits for
 * any events for OTA agent and process them. The loop never returns until the OTA agent
 * is shutdown. The tasks exits gracefully by freeing up all resources in the event of an
 *  OTA agent shutdown.
 *
 * @param[in] pvParam Any parameters to be passed to OTA agent task.
 */
static void prvOTAAgentTask( void * pvParam );


/**
 * @brief The function which runs the OTA update task.
 *
 * The demo task initializes the OTA agent an loops until OTA agent is shutdown.
 * It reports OTA update statistics (which includes number of blocks received, processed and dropped),
 * at regular intervals.
 *
 * @param[in] pvParam Any parameters to be passed to OTA demo task.
 */
void vOTAUpdateTask( void * pvParam );

/**
 * @brief Callback invoked for firmware image chunks received from MQTT broker.
 *
 * Function gets invoked for the firmware image blocks received on OTA data stream topic.
 * The function is registered with MQTT agent's subscription manager along with the
 * topic filter for data stream. For each packet received, the
 * function fetches a free event buffer from the pool and queues the firmware image chunk for
 * OTA agent task processing.
 *
 * @param[in] pxSubscriptionContext Context which is passed unmodified from the MQTT agent.
 * @param[in] pPublishInfo Pointer to the structure containing the details of the MQTT packet.
 */
static void prvProcessIncomingData( void * pxSubscriptionContext,
                                    MQTTPublishInfo_t * pPublishInfo );

/**
 * @brief Callback invoked for job control messages from MQTT broker.
 *
 * Callback gets invoked for any OTA job related control messages from the MQTT broker.
 * The function is registered with MQTT agent's subscription manager along with the topic filter for
 * job stream. The function fetches a free event buffer from the pool and queues the appropriate event type
 * based on the control message received.
 *
 * @param[in] pxSubscriptionContext Context which is passed unmodified from the MQTT agent.
 * @param[in] pPublishInfo Pointer to the structure containing the details of MQTT packet.
 */
static void prvProcessIncomingJobMessage( void * pxSubscriptionContext,
                                          MQTTPublishInfo_t * pPublishInfo );

/**
 * @brief Matches a client identifier within an OTA topic.
 * This function is used to validate that topic is valid and intended for this device thing name.
 *
 * @param[in] pTopic Pointer to the topic
 * @param[in] topicNameLength length of the topic
 * @param[in] pClientIdentifier Client identifier, should be null terminated.
 * @param[in] clientIdentifierLength Length of the client identifier.
 * @return pdTRUE if client identifier is found within the topic at the right index.
 */
static BaseType_t prvMatchClientIdentifierInTopic( const char * pTopic,
                                                   size_t topicNameLength,
                                                   const char * pClientIdentifier,
                                                   size_t clientIdentifierLength );


/**
 * @brief Returns pdTRUE if the OTA Agent is currently executing a job.
 * @return pdTRUE if OTA agent is currently active.
 */
static inline BaseType_t xIsOtaAgentActive( void );

/**
 * @brief Static buffer allocated by application and used by OTA Agent.
 * Buffer is allocated in the global scope outside of function call stack.
 */
static OtaAppStaticBuffer_t xAppStaticBuffer = { 0 };

/**
 * @brief Pointer which holds the thing name received from key value store.
 */

static char * pcThingName = NULL;

/**
 * @brief Variable which holds the length of the thing name.
 */
static size_t uxThingNameLength = 0UL;

/*---------------------------------------------------------*/

static BaseType_t prvOTAEventBufferPoolInit( OtaEventBufferPool_t * pxBufferPool )
{
    BaseType_t poolInit = pdFALSE;

    configASSERT( pxBufferPool != NULL );

    memset( pxBufferPool->eventBuffer, 0x00, sizeof( pxBufferPool->eventBuffer ) );

    pxBufferPool->lock = xSemaphoreCreateMutex();

    if( pxBufferPool->lock != NULL )
    {
        poolInit = pdTRUE;
    }

    return poolInit;
}

/*---------------------------------------------------------*/

static void prvOTAEventBufferFree( OtaEventBufferPool_t * pxBufferPool,
                                   OtaEventData_t * const pxBuffer )
{
    configASSERT( pxBufferPool != NULL );

    if( xSemaphoreTake( pxBufferPool->lock, portMAX_DELAY ) == pdTRUE )
    {
        pxBuffer->bufferUsed = false;
        ( void ) xSemaphoreGive( pxBufferPool->lock );
    }
    else
    {
        LogError( ( "Failed to get buffer semaphore." ) );
    }
}

/*-----------------------------------------------------------*/

static OtaEventData_t * prvOTAEventBufferGet( OtaEventBufferPool_t * pxBufferPool )
{
    uint32_t ulIndex = 0;
    OtaEventData_t * pFreeBuffer = NULL;

    configASSERT( pxBufferPool != NULL );

    if( xSemaphoreTake( pxBufferPool->lock, portMAX_DELAY ) == pdTRUE )
    {
        for( ulIndex = 0; ulIndex < otaconfigMAX_NUM_OTA_DATA_BUFFERS; ulIndex++ )
        {
            if( pxBufferPool->eventBuffer[ ulIndex ].bufferUsed == false )
            {
                pxBufferPool->eventBuffer[ ulIndex ].bufferUsed = true;
                pFreeBuffer = &pxBufferPool->eventBuffer[ ulIndex ];
                break;
            }
        }

        ( void ) xSemaphoreGive( pxBufferPool->lock );
    }
    else
    {
        LogError( ( "Failed to get buffer semaphore." ) );
    }

    return pFreeBuffer;
}

/*-----------------------------------------------------------*/
static void prvOTAAgentTask( void * pvParam )
{
    OTA_EventProcessingTask( pvParam );
    vTaskDelete( NULL );
}

/*-----------------------------------------------------------*/

#ifdef TFM_PSA_API
    static bool prvGetImageInfo( uint8_t ucSlot,
                                 uint32_t ulImageType,
                                 psa_image_info_t * pImageInfo )
    {
        psa_status_t xPSAStatus;
        bool xStatus = false;
        psa_image_id_t ulImageID = FWU_CALCULATE_IMAGE_ID( ucSlot, ulImageType, 0 );

        xPSAStatus = psa_fwu_query( ulImageID, pImageInfo );

        if( xPSAStatus == PSA_SUCCESS )
        {
            xStatus = true;
        }
        else
        {
            LogError( "Failed to query image info for slot %u", ucSlot );
            xStatus = false;
        }

        return xStatus;
    }
#endif /* ifdef TFM_PSA_API */

/*-----------------------------------------------------------*/

#ifdef TFM_PSA_API

/**
 * @brief Checks versions if active version has higher version than stage version.
 *
 * @param[in] pActiveVersion Active version.
 * @param[in] pStageVersion Stage version.
 *
 * @return true if active version is higher than stage version. false otherwise.
 *
 */
    static bool prvCheckVersion( psa_image_info_t * pActiveVersion,
                                 psa_image_info_t * pStageVersion )
    {
        bool xStatus = false;
        AppVersion32_t xActiveFirmwareVersion = { 0 };
        AppVersion32_t xStageFirmwareVersion = { 0 };

        xActiveFirmwareVersion.u.x.major = pActiveVersion->version.iv_major;
        xActiveFirmwareVersion.u.x.minor = pActiveVersion->version.iv_minor;
        xActiveFirmwareVersion.u.x.build = ( uint16_t ) pActiveVersion->version.iv_revision;

        xStageFirmwareVersion.u.x.major = pStageVersion->version.iv_major;
        xStageFirmwareVersion.u.x.minor = pStageVersion->version.iv_minor;
        xStageFirmwareVersion.u.x.build = ( uint16_t ) pStageVersion->version.iv_revision;

        if( xActiveFirmwareVersion.u.unsignedVersion32 > xStageFirmwareVersion.u.unsignedVersion32 )
        {
            xStatus = true;
        }

        return xStatus;
    }
#endif /* ifdef TFM_PSA_API */

/*-----------------------------------------------------------*/

#ifdef TFM_PSA_API

/**
 * @brief Checks versions of an image type for rollback protection.
 *
 * @param[in] ulImageType Image Type for which the version needs to be checked.
 *
 * @return true if the version is higher than previous version. false otherwise.
 *
 */
    static bool prvImageVersionCheck( uint32_t ulImageType )
    {
        psa_image_info_t xActiveImageInfo = { 0 };
        psa_image_info_t xStageImageInfo = { 0 };

        bool xStatus = false;

        xStatus = prvGetImageInfo( FWU_IMAGE_ID_SLOT_ACTIVE, ulImageType, &xActiveImageInfo );

        if( ( xStatus == true ) && ( xActiveImageInfo.state == PSA_IMAGE_PENDING_INSTALL ) )
        {
            xStatus = prvGetImageInfo( FWU_IMAGE_ID_SLOT_STAGE, ulImageType, &xStageImageInfo );

            if( xStatus == true )
            {
                xStatus = prvCheckVersion( &xActiveImageInfo, &xStageImageInfo );

                if( xStatus == false )
                {
                    LogError( "PSA Image type %d version validation failed, old version: %u.%u.%u new version: %u.%u.%u",
                              ulImageType,
                              xStageImageInfo.version.iv_major,
                              xStageImageInfo.version.iv_minor,
                              xStageImageInfo.version.iv_revision,
                              xActiveImageInfo.version.iv_major,
                              xActiveImageInfo.version.iv_minor,
                              xActiveImageInfo.version.iv_revision );
                }
                else
                {
                    LogError( "PSA Image type %d version validation succeeded, old version: %u.%u.%u new version: %u.%u.%u",
                              ulImageType,
                              xStageImageInfo.version.iv_major,
                              xStageImageInfo.version.iv_minor,
                              xStageImageInfo.version.iv_revision,
                              xActiveImageInfo.version.iv_major,
                              xActiveImageInfo.version.iv_minor,
                              xActiveImageInfo.version.iv_revision );
                }
            }
        }

        return xStatus;
    }
#endif /* ifdef TFM_PSA_API */

/*-----------------------------------------------------------*/

#ifdef TFM_PSA_API

/**
 * @brief Get Secure and Non Secure Image versions.
 *
 * @param[out] pSecureVersion Pointer to secure version struct.
 * @param[out] pNonSecureVersion Pointer to non-secure version struct.
 *
 * @return true if version was fetched successfully.
 *
 */
    static bool prvGetImageVersion( AppVersion32_t * pSecureVersion,
                                    AppVersion32_t * pNonSecureVersion )
    {
        psa_image_info_t xImageInfo = { 0 };
        bool xStatus = false;

        xStatus = prvGetImageInfo( FWU_IMAGE_ID_SLOT_ACTIVE, FWU_IMAGE_TYPE_SECURE, &xImageInfo );

        if( xStatus == true )
        {
            pSecureVersion->u.x.major = xImageInfo.version.iv_major;
            pSecureVersion->u.x.minor = xImageInfo.version.iv_minor;
            pSecureVersion->u.x.build = ( uint16_t ) xImageInfo.version.iv_revision;
        }

        if( xStatus == true )
        {
            xStatus = prvGetImageInfo( FWU_IMAGE_ID_SLOT_ACTIVE, FWU_IMAGE_TYPE_NONSECURE, &xImageInfo );
        }

        if( xStatus == true )
        {
            pNonSecureVersion->u.x.major = xImageInfo.version.iv_major;
            pNonSecureVersion->u.x.minor = xImageInfo.version.iv_minor;
            pNonSecureVersion->u.x.build = ( uint16_t ) xImageInfo.version.iv_revision;
        }

        return xStatus;
    }
#endif /* ifdef TFM_PSA_API */

/*-----------------------------------------------------------*/

/**
 * @brief The OTA agent has completed the update job or it is in
 * self test mode. If it was accepted, we want to activate the new image.
 * This typically means we should reset the device to run the new firmware.
 * If now is not a good time to reset the device, it may be activated later
 * by your user code. If the update was rejected, just return without doing
 * anything and we will wait for another job. If it reported that we should
 * start test mode, normally we would perform some kind of system checks to
 * make sure our new firmware does the basic things we think it should do
 * but we will just go ahead and set the image as accepted for demo purposes.
 * The accept function varies depending on your platform. Refer to the OTA
 * PAL implementation for your platform in aws_ota_pal.c to see what it
 * does for you.
 *
 * @param[in] event Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @param[in] pData Data associated with the event.
 * @return None.
 */
static void otaAppCallback( OtaJobEvent_t event,
                            const void * pData )
{
    OtaErr_t err = OtaErrUninitialized;

    switch( event )
    {
        case OtaJobEventActivate:
            LogInfo( ( "Received OtaJobEventActivate callback from OTA Agent." ) );

            /**
             * Activate the new firmware image immediately. Applications can choose to postpone
             * the activation to a later stage if needed.
             */
            err = OTA_ActivateNewImage();

            /**
             * Activation of the new image failed. This indicates an error that requires a follow
             * up through manual activation by resetting the device. The demo reports the error
             * and shuts down the OTA agent.
             */
            LogError( ( "New image activation failed." ) );
            break;

        case OtaJobEventFail:

            /**
             * No user action is needed here. OTA agent handles the job failure event.
             */
            LogInfo( ( "Received an OtaJobEventFail notification from OTA Agent." ) );

            break;

        case OtaJobEventStartTest:

            /* This demo just accepts the image since it was a good OTA update and networking
             * and services are all working (or we would not have made it this far). If this
             * were some custom device that wants to test other things before validating new
             * image, this would be the place to kick off those tests before calling
             * OTA_SetImageState() with the final result of either accepted or rejected. */

            LogInfo( ( "Received OtaJobEventStartTest callback from OTA Agent." ) );

            #ifdef TFM_PSA_API
            {
                /*
                 * Do version check validation here, given that OTA Agent library does not handle
                 * runtime version check of secure or non-secure images.
                 */
                if( ( prvImageVersionCheck( FWU_IMAGE_TYPE_SECURE ) == true ) &&
                    ( prvImageVersionCheck( FWU_IMAGE_TYPE_NONSECURE ) == true ) )
                {
                    err = OTA_SetImageState( OtaImageStateAccepted );
                }
                else
                {
                    err = OTA_SetImageState( OtaImageStateRejected );

                    if( err == OtaErrNone )
                    {
                        /* Slight delay to flush the logs. */
                        vTaskDelay( pdMS_TO_TICKS( 500 ) );
                        /*  Reset the device, to revert back to the old image. */
                        psa_fwu_request_reboot();
                        LogError( ( "Failed to reset the device to revert the image." ) );
                    }
                    else
                    {
                        LogError( ( "Unable to reject the image which failed self test." ) );
                    }
                }
            }
            #else /* ifdef TFM_PSA_API */
            {
                err = OTA_SetImageState( OtaImageStateAccepted );
            }
            #endif /* ifdef TFM_PSA_API */

            if( err == OtaErrNone )
            {
                LogInfo( ( "New image validation succeeded in self test mode." ) );
            }
            else
            {
                LogError( ( "Failed to set image state as accepted with error %d.", err ) );
            }

            break;

        case OtaJobEventProcessed:

            LogDebug( ( "OTA Event processing completed. Freeing the event buffer to pool." ) );
            configASSERT( pData != NULL );
            prvOTAEventBufferFree( &xAppStaticBuffer.eventBufferPool, ( OtaEventData_t * ) pData );

            break;

        case OtaJobEventSelfTestFailed:
            LogDebug( ( "Received OtaJobEventSelfTestFailed callback from OTA Agent." ) );

            /* Requires manual activation of previous image as self-test for
             * new image downloaded failed.*/
            LogError( ( "OTA Self-test failed for new image. shutting down OTA Agent." ) );
            break;

        case OtaJobEventUpdateComplete:
            LogInfo( "OTA Update Complete" );
            break;

        default:
            LogWarn( ( "Received an unhandled callback event from OTA Agent, event = %d", event ) );

            break;
    }
}

/*-----------------------------------------------------------*/

static void prvProcessIncomingData( void * pxContext,
                                    MQTTPublishInfo_t * pPublishInfo )
{
    BaseType_t isMatch = pdFALSE;
    OtaEventData_t * pData;
    OtaEventMsg_t eventMsg = { 0 };

    ( void ) pxContext;

    configASSERT( pPublishInfo != NULL );


    isMatch = prvMatchClientIdentifierInTopic( pPublishInfo->pTopicName,
                                               pPublishInfo->topicNameLength,
                                               pcThingName,
                                               uxThingNameLength );

    if( isMatch == pdTRUE )
    {
        if( pPublishInfo->payloadLength <= OTA_DATA_BLOCK_SIZE )
        {
            LogDebug( ( "Received OTA image block, size %d.\n\n", pPublishInfo->payloadLength ) );

            pData = prvOTAEventBufferGet( &xAppStaticBuffer.eventBufferPool );

            if( pData != NULL )
            {
                memcpy( pData->data, pPublishInfo->pPayload, pPublishInfo->payloadLength );
                pData->dataLength = pPublishInfo->payloadLength;
                eventMsg.eventId = OtaAgentEventReceivedFileBlock;
                eventMsg.pEventData = pData;

                /* Send job document received event. */
                OTA_SignalEvent( &eventMsg );
            }
            else
            {
                LogError( ( "Error: No OTA data buffers available.\r\n" ) );
            }
        }
        else
        {
            LogError( ( "Received OTA data block of size (%d) larger than maximum size(%d) defined. ",
                        pPublishInfo->payloadLength,
                        OTA_DATA_BLOCK_SIZE ) );
        }
    }
    else
    {
        LogWarn( ( "Received data block on an unsolicited topic, thing name does not match. topic: %.*s ",
                   pPublishInfo->topicNameLength,
                   pPublishInfo->pTopicName ) );
    }
}

/*-----------------------------------------------------------*/

static void prvProcessIncomingJobMessage( void * pxSubscriptionContext,
                                          MQTTPublishInfo_t * pPublishInfo )
{
    OtaEventData_t * pData;
    OtaEventMsg_t eventMsg = { 0 };
    BaseType_t isMatch = pdFALSE;

    ( void ) pxSubscriptionContext;
    configASSERT( pPublishInfo != NULL );
    configASSERT( pcThingName != NULL );

    isMatch = prvMatchClientIdentifierInTopic( pPublishInfo->pTopicName,
                                               pPublishInfo->topicNameLength,
                                               pcThingName,
                                               strlen( pcThingName ) );

    if( isMatch == pdTRUE )
    {
        if( pPublishInfo->payloadLength <= OTA_DATA_BLOCK_SIZE )
        {
            LogInfo( ( "Received OTA job message, size: %d.\n\n", pPublishInfo->payloadLength ) );
            pData = prvOTAEventBufferGet( &xAppStaticBuffer.eventBufferPool );

            if( pData != NULL )
            {
                memcpy( pData->data, pPublishInfo->pPayload, pPublishInfo->payloadLength );
                pData->dataLength = pPublishInfo->payloadLength;
                eventMsg.eventId = OtaAgentEventReceivedJobDocument;
                eventMsg.pEventData = pData;

                /* Send job document received event. */
                OTA_SignalEvent( &eventMsg );
            }
            else
            {
                LogError( ( "Error: No OTA data buffers available.\r\n" ) );
            }
        }
        else
        {
            LogError( ( "Received OTA job message size (%d) is larger than the OTA maximum size (%d) defined.\n\n", pPublishInfo->payloadLength, OTA_DATA_BLOCK_SIZE ) );
        }
    }
    else
    {
        LogWarn( ( "Received a job message on an unsolicited topic, thing name does not match. topic: %.*s ",
                   pPublishInfo->topicNameLength,
                   pPublishInfo->pTopicName ) );
    }
}


/*-----------------------------------------------------------*/


static IncomingPubCallback_t prvGetPublishCallbackFromTopic( const char * pcTopicFilter,
                                                             size_t usTopicFilterLength )
{
    bool xIsMatch = false;
    IncomingPubCallback_t xCallback = NULL;


    ( void ) MQTT_MatchTopic( pcTopicFilter,
                              usTopicFilterLength,
                              OTA_JOB_TOPIC_FILTER,
                              OTA_JOB_TOPIC_FILTER_LEN,
                              &xIsMatch );

    if( xIsMatch == true )
    {
        xCallback = prvProcessIncomingJobMessage;
    }

    if( xIsMatch == false )
    {
        ( void ) MQTT_MatchTopic( pcTopicFilter,
                                  usTopicFilterLength,
                                  OTA_DATA_STREAM_TOPIC_FILTER,
                                  OTA_DATA_STREAM_TOPIC_FILTER_LENGTH,
                                  &xIsMatch );

        if( xIsMatch == true )
        {
            xCallback = prvProcessIncomingData;
        }
    }

    return xCallback;
}

/*-----------------------------------------------------------*/

static BaseType_t prvMatchClientIdentifierInTopic( const char * pTopic,
                                                   size_t topicNameLength,
                                                   const char * pClientIdentifier,
                                                   size_t clientIdentifierLength )
{
    BaseType_t isMatch = pdFALSE;
    size_t idx, matchIdx = 0;

    for( idx = OTA_TOPIC_CLIENT_IDENTIFIER_START_IDX; idx < topicNameLength; idx++ )
    {
        if( matchIdx == clientIdentifierLength )
        {
            if( pTopic[ idx ] == '/' )
            {
                isMatch = pdTRUE;
            }

            break;
        }
        else
        {
            if( pClientIdentifier[ matchIdx ] != pTopic[ idx ] )
            {
                break;
            }
        }

        matchIdx++;
    }

    return isMatch;
}

/*-----------------------------------------------------------*/

static void prvCommandCallback( MQTTAgentCommandContext_t * pCommandContext,
                                MQTTAgentReturnInfo_t * pxReturnInfo )
{
    configASSERT( pCommandContext != NULL );
    configASSERT( pCommandContext->xTaskToNotify != NULL );
    configASSERT( pxReturnInfo != NULL );

    ( void ) xTaskNotify( pCommandContext->xTaskToNotify, ( uint32_t ) ( pxReturnInfo->returnCode ), eSetValueWithOverwrite );
}


/*-----------------------------------------------------------*/

static OtaMqttStatus_t prvMQTTSubscribe( const char * pTopicFilter,
                                         uint16_t topicFilterLength,
                                         uint8_t ucQoS )
{
    MQTTStatus_t mqttStatus;
    OtaMqttStatus_t otaRet = OtaMqttSuccess;
    IncomingPubCallback_t xPublishCallback;
    MQTTAgentHandle_t xMQTTAgentHandle = NULL;

    configASSERT( pTopicFilter != NULL );
    configASSERT( topicFilterLength > 0 );

    xPublishCallback = prvGetPublishCallbackFromTopic( pTopicFilter, topicFilterLength );

    xMQTTAgentHandle = xGetMqttAgentHandle();

    if( ( xMQTTAgentHandle == NULL ) ||
        ( xPublishCallback == NULL ) )
    {
        otaRet = OtaMqttSubscribeFailed;
    }
    else
    {
        mqttStatus = MqttAgent_SubscribeSync( xMQTTAgentHandle,
                                              pTopicFilter,
                                              ucQoS,
                                              xPublishCallback,
                                              NULL );

        if( mqttStatus != MQTTSuccess )
        {
            LogError( ( "Failed to SUBSCRIBE to topic with error = %u.",
                        mqttStatus ) );

            otaRet = OtaMqttSubscribeFailed;
        }
        else
        {
            LogInfo( ( "Subscribed to topic %.*s.\n\n",
                       topicFilterLength,
                       pTopicFilter ) );

            otaRet = OtaMqttSuccess;
        }
    }

    return otaRet;
}

static OtaMqttStatus_t prvMQTTPublish( const char * const pacTopic,
                                       uint16_t topicLen,
                                       const char * pMsg,
                                       uint32_t msgSize,
                                       uint8_t qos )
{
    OtaMqttStatus_t otaRet = OtaMqttSuccess;
    BaseType_t result;
    MQTTStatus_t mqttStatus = MQTTBadParameter;
    MQTTPublishInfo_t publishInfo = { 0 };
    MQTTAgentCommandInfo_t xCommandParams = { 0 };
    uint32_t ulNotifyValue = 0UL;
    MQTTAgentCommandContext_t xCommandContext = { 0 };
    MQTTAgentHandle_t xMQTTAgentHandle = NULL;

    publishInfo.pTopicName = pacTopic;
    publishInfo.topicNameLength = topicLen;
    publishInfo.qos = qos;
    publishInfo.pPayload = pMsg;
    publishInfo.payloadLength = msgSize;


    xTaskNotifyStateClear( NULL );

    xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();

    xCommandParams.blockTimeMs = otaexampleMQTT_TIMEOUT_MS;
    xCommandParams.cmdCompleteCallback = prvCommandCallback;
    xCommandParams.pCmdCompleteCallbackContext = &xCommandContext;

    xMQTTAgentHandle = xGetMqttAgentHandle();

    if( xMQTTAgentHandle == NULL )
    {
        otaRet = OtaMqttPublishFailed;
    }
    else
    {
        mqttStatus = MQTTAgent_Publish( xMQTTAgentHandle,
                                        &publishInfo,
                                        &xCommandParams );

        /* Wait for command to complete so MQTTSubscribeInfo_t remains in scope for the
         * duration of the command. */
        if( mqttStatus == MQTTSuccess )
        {
            result = xTaskNotifyWait( 0, otaexampleMAX_UINT32, &ulNotifyValue, pdMS_TO_TICKS( otaexampleMQTT_TIMEOUT_MS ) );

            if( result != pdTRUE )
            {
                mqttStatus = MQTTSendFailed;
            }
            else
            {
                mqttStatus = ( MQTTStatus_t ) ( ulNotifyValue );
            }
        }

        if( mqttStatus != MQTTSuccess )
        {
            LogError( ( "Failed to send PUBLISH packet to broker with error = %u.", mqttStatus ) );
            otaRet = OtaMqttPublishFailed;
        }
        else
        {
            LogInfo( ( "Sent PUBLISH packet to broker %.*s to broker.\n\n",
                       topicLen,
                       pacTopic ) );

            otaRet = OtaMqttSuccess;
        }
    }

    return otaRet;
}

static OtaMqttStatus_t prvMQTTUnsubscribe( const char * pTopicFilter,
                                           uint16_t topicFilterLength,
                                           uint8_t ucQoS )
{
    MQTTStatus_t mqttStatus;
    OtaMqttStatus_t otaRet = OtaMqttSuccess;
    IncomingPubCallback_t xPublishCallback;
    MQTTAgentHandle_t xMQTTAgentHandle = NULL;

    configASSERT( pTopicFilter != NULL );
    configASSERT( topicFilterLength > 0 );

    xPublishCallback = prvGetPublishCallbackFromTopic( pTopicFilter, topicFilterLength );

    xMQTTAgentHandle = xGetMqttAgentHandle();

    if( ( xMQTTAgentHandle == NULL ) ||
        ( xPublishCallback == NULL ) )
    {
        otaRet = OtaMqttUnsubscribeFailed;
    }
    else
    {
        mqttStatus = MqttAgent_UnSubscribeSync( xMQTTAgentHandle,
                                                pTopicFilter,
                                                xPublishCallback,
                                                NULL );

        if( mqttStatus != MQTTSuccess )
        {
            LogError( ( "Failed to UNSUBSCRIBE from topic %.*s with error = %u.",
                        topicFilterLength,
                        pTopicFilter,
                        mqttStatus ) );

            otaRet = OtaMqttUnsubscribeFailed;
        }
        else
        {
            LogInfo( ( "UNSUBSCRIBED from topic %.*s.\n\n",
                       topicFilterLength,
                       pTopicFilter ) );

            otaRet = OtaMqttSuccess;
        }
    }

    return otaRet;
}

/*-----------------------------------------------------------*/

static void prvSetOtaInterfaces( OtaInterfaces_t * pOtaInterfaces )
{
    configASSERT( pOtaInterfaces != NULL );

    /* Initialize OTA library OS Interface. */
    pOtaInterfaces->os.event.init = OtaInitEvent_FreeRTOS;
    pOtaInterfaces->os.event.send = OtaSendEvent_FreeRTOS;
    pOtaInterfaces->os.event.recv = OtaReceiveEvent_FreeRTOS;
    pOtaInterfaces->os.event.deinit = OtaDeinitEvent_FreeRTOS;
    pOtaInterfaces->os.timer.start = OtaStartTimer_FreeRTOS;
    pOtaInterfaces->os.timer.stop = OtaStopTimer_FreeRTOS;
    pOtaInterfaces->os.timer.delete = OtaDeleteTimer_FreeRTOS;
    pOtaInterfaces->os.mem.malloc = Malloc_FreeRTOS;
    pOtaInterfaces->os.mem.free = Free_FreeRTOS;

    /* Initialize the OTA library MQTT Interface.*/
    pOtaInterfaces->mqtt.subscribe = prvMQTTSubscribe;
    pOtaInterfaces->mqtt.publish = prvMQTTPublish;
    pOtaInterfaces->mqtt.unsubscribe = prvMQTTUnsubscribe;

    /* Initialize the OTA library PAL Interface.*/
    pOtaInterfaces->pal.getPlatformImageState = otaPal_GetPlatformImageState;
    pOtaInterfaces->pal.setPlatformImageState = otaPal_SetPlatformImageState;
    pOtaInterfaces->pal.writeBlock = otaPal_WriteBlock;
    pOtaInterfaces->pal.activate = otaPal_ActivateNewImage;
    pOtaInterfaces->pal.closeFile = otaPal_CloseFile;
    pOtaInterfaces->pal.reset = otaPal_ResetDevice;
    pOtaInterfaces->pal.abort = otaPal_Abort;
    pOtaInterfaces->pal.createFile = otaPal_CreateFileForRx;
}

static void prvSetOTAAppBuffer( OtaAppBuffer_t * pOtaAppBuffer )
{
    pOtaAppBuffer->pUpdateFilePath = xAppStaticBuffer.updateFilePath;
    pOtaAppBuffer->updateFilePathsize = otaexampleMAX_FILE_PATH_SIZE;
    pOtaAppBuffer->pCertFilePath = xAppStaticBuffer.certFilePath;
    pOtaAppBuffer->certFilePathSize = otaexampleMAX_FILE_PATH_SIZE;
    pOtaAppBuffer->pStreamName = xAppStaticBuffer.streamName;
    pOtaAppBuffer->streamNameSize = otaexampleMAX_STREAM_NAME_SIZE;
    pOtaAppBuffer->pDecodeMemory = xAppStaticBuffer.decodeMem;
    pOtaAppBuffer->decodeMemorySize = ( 1U << otaconfigLOG2_FILE_BLOCK_SIZE );
    pOtaAppBuffer->pFileBitmap = xAppStaticBuffer.bitmap;
    pOtaAppBuffer->fileBitmapSize = OTA_MAX_BLOCK_BITMAP_SIZE;
}

static inline BaseType_t xIsOtaAgentActive( void )
{
    BaseType_t xResult;

    switch( OTA_GetState() )
    {
        case OtaAgentStateRequestingJob:
        case OtaAgentStateCreatingFile:
        case OtaAgentStateRequestingFileBlock:
        case OtaAgentStateWaitingForFileBlock:
        case OtaAgentStateClosingFile:
            xResult = pdTRUE;
            break;

        case OtaAgentStateWaitingForJob:
        case OtaAgentStateNoTransition:
        case OtaAgentStateInit:
        case OtaAgentStateReady:
        case OtaAgentStateStopped:
        case OtaAgentStateSuspended:
        case OtaAgentStateShuttingDown:
        default:
            xResult = pdFALSE;
            break;
    }

    return xResult;
}

void vOTAUpdateTask( void * pvParam )
{
    ( void ) pvParam;
    /* FreeRTOS APIs return status. */
    BaseType_t xResult = pdPASS;

    /* OTA library return status. */
    OtaErr_t otaRet = OtaErrNone;

    /* OTA event message used for sending event to OTA Agent.*/
    OtaEventMsg_t eventMsg = { 0 };

    /* OTA interface context required for library interface functions.*/
    OtaInterfaces_t otaInterfaces;

    MQTTStatus_t xMQTTStatus = MQTTBadParameter;

    MQTTAgentHandle_t xMQTTAgentHandle = NULL;

    /**
     * @brief Structure containing all application allocated buffers used by the OTA agent.
     * Structure is passed to the OTA agent during initialization.
     */
    OtaAppBuffer_t otaAppBuffer = { 0 };

    /* Set OTA Library interfaces.*/
    prvSetOtaInterfaces( &otaInterfaces );

    /* Set OTA buffers for use by OTA agent. */
    prvSetOTAAppBuffer( &otaAppBuffer );

    #ifndef TFM_PSA_API
    {
        /*
         * Application defined firmware version is only used in Non-Trustzone.
         */

        LogInfo( ( "OTA Agent: Application version %u.%u.%u",
                   appFirmwareVersion.u.x.major,
                   appFirmwareVersion.u.x.minor,
                   appFirmwareVersion.u.x.build ) );
    }
    #else
    {
        AppVersion32_t xSecureVersion = { 0 }, xNSVersion = { 0 };
        prvGetImageVersion( &xSecureVersion, &xNSVersion );
        LogInfo( ( "OTA Agent: Secure Image version %u.%u.%u, Non-secure Image Version: %u.%u.%u",
                   xSecureVersion.u.x.major,
                   xSecureVersion.u.x.minor,
                   xSecureVersion.u.x.build,
                   xNSVersion.u.x.major,
                   xNSVersion.u.x.minor,
                   xNSVersion.u.x.build ) );
    }
    #endif /* ifndef TFM_PSA_API */


    /****************************** Init OTA Library. ******************************/

    if( xResult == pdPASS )
    {
        /* Fetch thing name from key value store. */
        pcThingName = KVStore_getStringHeap( CS_CORE_THING_NAME, &uxThingNameLength );

        if( ( pcThingName == NULL ) ||
            ( uxThingNameLength == 0 ) )
        {
            xResult = pdFAIL;
            LogError( ( "Failed to load thing name from key value store." ) );
        }
    }

    if( xResult == pdPASS )
    {
        EventBits_t uxEvents;
        LogInfo( "Waiting until MQTT Agent is connected." );

        uxEvents = xEventGroupWaitBits( xSystemEvents,
                                        EVT_MASK_MQTT_CONNECTED,
                                        pdFALSE,
                                        pdTRUE,
                                        pdMS_TO_TICKS( otaexampleOTA_UPDATE_TIMEOUT_MS ) );

        if( uxEvents & EVT_MASK_MQTT_CONNECTED )
        {
            LogInfo( "MQTT Agent is connected. Resuming..." );
            xMQTTAgentHandle = xGetMqttAgentHandle();
        }
        else
        {
            xMQTTAgentHandle = NULL;
            LogInfo( "Timed out while waiting for MQTT Agent connection." );
        }
    }

    if( ( xResult == pdPASS ) &&
        ( xMQTTAgentHandle != NULL ) )
    {
        xMQTTStatus = MqttAgent_SubscribeSync( xMQTTAgentHandle,
                                               OTA_JOB_ACCEPTED_RESPONSE_TOPIC_FILTER,
                                               MQTTQoS0,
                                               prvProcessIncomingJobMessage,
                                               NULL );

        if( xMQTTStatus != MQTTSuccess )
        {
            LogError( "Failed to subscribe to Job Accepted response topic filter." );
            xResult = pdFAIL;
        }
    }

    if( ( xResult == pdPASS ) &&
        ( xMQTTAgentHandle != NULL ) )
    {
        xMQTTStatus = MqttAgent_SubscribeSync( xMQTTAgentHandle,
                                               OTA_JOB_NOTIFY_TOPIC_FILTER,
                                               MQTTQoS0,
                                               prvProcessIncomingJobMessage,
                                               NULL );

        if( xMQTTStatus != MQTTSuccess )
        {
            LogError( "Failed to subscribe to Job Update topic filter." );
            xResult = pdFAIL;
        }
    }

    if( xResult == pdPASS )
    {
        /* Initialize event buffer pool. */
        xResult = prvOTAEventBufferPoolInit( &xAppStaticBuffer.eventBufferPool );
    }

    if( xResult == pdPASS )
    {
        if( ( otaRet = OTA_Init( &otaAppBuffer,
                                 &otaInterfaces,
                                 ( const uint8_t * ) ( pcThingName ),
                                 otaAppCallback ) ) != OtaErrNone )
        {
            LogError( ( "Failed to initialize OTA Agent, exiting = %u.",
                        otaRet ) );
            xResult = pdFAIL;
        }
    }

    if( xResult == pdPASS )
    {
        if( ( xResult = xTaskCreate( prvOTAAgentTask,
                                     "OTAAgent",
                                     otaexampleAGENT_TASK_STACK_SIZE,
                                     NULL,
                                     otaexampleAGENT_TASK_PRIORITY,
                                     NULL ) ) != pdPASS )
        {
            LogError( ( "Failed to start OTA Agent task: "
                        ",errno=%d",
                        xResult ) );
        }
    }

    /***************************Start OTA demo loop. ******************************/

    if( xResult == pdPASS )
    {
        /* Start the OTA Agent.*/
        eventMsg.eventId = OtaAgentEventStart;
        OTA_SignalEvent( &eventMsg );

        do
        {
            /* OTA library packet statistics per job.*/
            OtaAgentStatistics_t otaStatistics = { 0 };

            /* Get OTA statistics for currently executing job. */
            if( ( xIsOtaAgentActive() == pdTRUE ) &&
                ( OTA_GetStatistics( &otaStatistics ) == OtaErrNone ) )
            {
                LogInfo( ( "State: %s   Received: %u   Queued: %u   Processed: %u   Dropped: %u",
                           pOtaAgentStateStrings[ OTA_GetState() ],
                           otaStatistics.otaPacketsReceived,
                           otaStatistics.otaPacketsQueued,
                           otaStatistics.otaPacketsProcessed,
                           otaStatistics.otaPacketsDropped ) );
            }

            vTaskDelay( pdMS_TO_TICKS( otaexampleTASK_DELAY_MS ) );
        } while( OTA_GetState() != OtaAgentStateStopped );
    }

    LogInfo( ( "OTA agent task stopped. Exiting OTA demo." ) );

    if( xMQTTStatus == MQTTSuccess )
    {
        xMQTTStatus = MqttAgent_UnSubscribeSync( xMQTTAgentHandle,
                                                 OTA_JOB_ACCEPTED_RESPONSE_TOPIC_FILTER,
                                                 prvProcessIncomingJobMessage,
                                                 NULL );

        if( xMQTTStatus != MQTTSuccess )
        {
            LogError( "MQTT unsubscribe request failed." );
        }
    }

    if( pcThingName != NULL )
    {
        vPortFree( pcThingName );
        pcThingName = NULL;
    }

    vTaskDelete( NULL );
}


void vSuspendOTAUpdate( void )
{
    if( ( OTA_GetState() != OtaAgentStateSuspended ) && ( OTA_GetState() != OtaAgentStateStopped ) )
    {
        OTA_Suspend();

        while( ( OTA_GetState() != OtaAgentStateSuspended ) &&
               ( OTA_GetState() != OtaAgentStateStopped ) )
        {
            vTaskDelay( pdMS_TO_TICKS( otaexampleTASK_DELAY_MS ) );
        }
    }
}

void vResumeOTAUpdate( void )
{
    if( OTA_GetState() == OtaAgentStateSuspended )
    {
        OTA_Resume();

        while( OTA_GetState() == OtaAgentStateSuspended )
        {
            vTaskDelay( pdMS_TO_TICKS( otaexampleTASK_DELAY_MS ) );
        }
    }
}
#endif
