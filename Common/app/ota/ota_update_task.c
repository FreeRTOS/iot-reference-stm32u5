
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
#include "MQTTFileDownloader_base64.h"
#include "jobs.h"
#include "mqtt_wrapper.h"
#include "ota_pal.h"
#include "ota_demo.h"
#include "ota_job_processor.h"
#include "os/ota_os_freertos.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "sys_evt.h"

#define CONFIG_MAX_FILE_SIZE    FLASH_BANK_SIZE
#define NUM_OF_BLOCKS_REQUESTED 1U
#define START_JOB_MSG_LENGTH    147U
#define MAX_THING_NAME_SIZE     128U
#define MAX_JOB_ID_LENGTH       64U
#define UPDATE_JOB_MSG_LENGTH   48U
#define MAX_NUM_OF_OTA_DATA_BUFFERS 5U
#define MAX_SIGNATURE_LENGTH_ECDSA  72U

MqttFileDownloaderContext_t mqttFileDownloaderContext = { 0 };
static uint32_t numOfBlocksRemaining = 0;
static uint32_t currentBlockOffset = 0;
static uint8_t currentFileId = 0;
static uint32_t totalBytesReceived = 0;
char globalJobId[ MAX_JOB_ID_LENGTH ] = { 0 };
extern EventGroupHandle_t xSystemEvents;

static OtaDataEvent_t dataBuffers[MAX_NUM_OF_OTA_DATA_BUFFERS] = { 0 };
static OtaJobEventData_t jobDocBuffer = { 0 };
static AfrOtaJobDocumentFields_t jobFields = { 0 };
static uint8_t OtaImageSingatureDecoded[MAX_SIGNATURE_LENGTH_ECDSA] = { 0 };
static SemaphoreHandle_t bufferSemaphore;

static OtaState_t otaAgentState = OtaAgentStateInit;

static bool closeFile( void );

static bool activateImage( void );

static bool sendSuccessMessage( void );

static void processOTAEvents( void );

static void requestJobDocumentHandler( void );

static OtaPalJobDocProcessingResult_t receivedJobDocumentHandler( OtaJobEventData_t * jobDoc );

static bool jobDocumentParser( char * message, size_t messageLength, AfrOtaJobDocumentFields_t *jobFields );

static void initMqttDownloader( AfrOtaJobDocumentFields_t *jobFields );

static OtaDataEvent_t * getOtaDataEventBuffer( void );

static void freeOtaDataEventBuffer( OtaDataEvent_t * const buffer );

static int16_t handleMqttStreamsBlockArrived( uint8_t *data, size_t dataLength );

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
										( uint16_t ) currentBlockOffset,
                                        NUM_OF_BLOCKS_REQUESTED,
                                        getStreamRequest,
                                        GET_STREAM_REQUEST_BUFFER_SIZE );

    mqttWrapper_publish( mqttFileDownloaderContext.topicGetStream,
                         mqttFileDownloaderContext.topicGetStreamLength,
                         ( uint8_t * ) getStreamRequest,
                         getStreamRequestLength );
}

/*-----------------------------------------------------------*/

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
                   ( uint16_t ) thingNameLength,
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
    currentFileId = ( uint8_t ) jobFields->fileId;
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

bool convertSignatureToDER(AfrOtaJobDocumentFields_t *jobFields)
{
	bool returnVal = true;
	size_t decodedSignatureLength = 0;


    Base64Status_t xResult = base64_Decode( OtaImageSingatureDecoded,
                              sizeof(OtaImageSingatureDecoded),
                              &decodedSignatureLength,
							  jobFields->signature,
							  jobFields->signatureLen );

    if( xResult == Base64Success )
    {
    	jobFields->signature = OtaImageSingatureDecoded;
    	jobFields->signatureLen = decodedSignatureLength;
    }
    else
    {
    	returnVal = false;
    }

    return returnVal;
}

static OtaPalJobDocProcessingResult_t receivedJobDocumentHandler( OtaJobEventData_t * jobDoc )
{
    bool parseJobDocument = false;
    bool handled = false;
    char * jobId;
    const char ** jobIdptr = &jobId;
    size_t jobIdLength = 0U;
    OtaPalJobDocProcessingResult_t xResult = OtaPalJobDocFileCreateFailed;

    memset( &jobFields,0 , sizeof(jobFields) );

    /*
     * AWS IoT Jobs library:
     * Extracting the job ID from the received OTA job document.
     */
    jobIdLength = Jobs_GetJobId( (char *)jobDoc->jobData, jobDoc->jobDataLength, jobIdptr );

    if ( jobIdLength )
    {
        if ( strncmp( globalJobId, jobId, jobIdLength ) )
        {
            parseJobDocument = true;
            strncpy( globalJobId, jobId, jobIdLength );
        }
        else
        {
        	xResult = OtaPalJobDocFileCreated;
        }
    }

    if ( parseJobDocument )
    {
        handled = jobDocumentParser( (char * )jobDoc->jobData, jobDoc->jobDataLength, &jobFields );
        if( handled )
        {
            initMqttDownloader( &jobFields );

            /* AWS IoT core returns the signature in a PEM format. We need to
             * convert it to DER format for image signature verification. */

            handled = convertSignatureToDER( &jobFields );

            if( handled )
            {
            	xResult = otaPal_CreateFileForRx( &jobFields );
            }
            else
            {
            	LogError( "Failed to decode the image signature to DER format." );
            }
        }
    }

    return xResult;
}


static uint16_t getFreeOTABuffers( void )
{
    uint32_t ulIndex = 0;
    uint16_t freeBuffers = 0;

    if( xSemaphoreTake( bufferSemaphore, portMAX_DELAY ) == pdTRUE )
    {
        for( ulIndex = 0; ulIndex < MAX_NUM_OF_OTA_DATA_BUFFERS; ulIndex++ )
        {
            if( dataBuffers[ ulIndex ].bufferUsed == false )
            {
            	freeBuffers++;
            }
        }

        ( void ) xSemaphoreGive( bufferSemaphore );
    }
    else
    {
        LogInfo("Failed to get buffer semaphore. \n" );
    }

    return freeBuffers;
}

static void processOTAEvents() {
    OtaEventMsg_t recvEvent = { 0 };
    OtaEvent_t recvEventId = 0;
    OtaEventMsg_t nextEvent = { 0 };

    OtaReceiveEvent_FreeRTOS(&recvEvent);
    recvEventId = recvEvent.eventId;

    switch (recvEventId)
    {
    case OtaAgentEventRequestJobDocument:
        LogInfo("Request Job Document event Received \n");
        LogInfo("-------------------------------------\n");
        requestJobDocumentHandler();
        otaAgentState = OtaAgentStateRequestingJob;
        break;

    case OtaAgentEventReceivedJobDocument:
        LogInfo("Received Job Document event Received \n");
        LogInfo("-------------------------------------\n");

        if (otaAgentState == OtaAgentStateSuspended)
        {
            LogInfo("OTA-Agent is in Suspend State. Hence dropping Job Document. \n");
            break;
        }

        switch( receivedJobDocumentHandler(recvEvent.jobEvent) )
        {
        case OtaPalJobDocFileCreated:
            LogInfo( "Received OTA Job. \n" );
            nextEvent.eventId = OtaAgentEventRequestFileBlock;
            OtaSendEvent_FreeRTOS( &nextEvent );
            otaAgentState = OtaAgentStateCreatingFile;
            break;

        case OtaPalJobDocFileCreateFailed:
        case OtaPalNewImageBootFailed:
        case OtaPalJobDocProcessingStateInvalid:
        	LogInfo("This is not an OTA job \n");
        	break;

        case OtaPalNewImageBooted:
        	( void ) sendSuccessMessage();
        	/* Short delay before restarting the loop. */
        	vTaskDelay( pdMS_TO_TICKS( 200 ) );

        	/* Get ready for new OTA job. */
        	nextEvent.eventId = OtaAgentEventRequestJobDocument;
        	OtaSendEvent_FreeRTOS( &nextEvent );
        	break;
        }


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
        LogInfo("ReqSent----------------------------\n");
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
        int16_t result;
        /*
         * MQTT streams Library:
         * Extracting and decoding the received data block from the incoming MQTT message.
         */
        //LogInfo("%s",recvEvent.dataEvent->data);
        mqttDownloader_processReceivedDataBlock(
            &mqttFileDownloaderContext,
            recvEvent.dataEvent->data,
            recvEvent.dataEvent->dataLength,
            decodedData,
            &decodedDataLength );

        result = handleMqttStreamsBlockArrived(decodedData, decodedDataLength);
        freeOtaDataEventBuffer(recvEvent.dataEvent);

        if( result > 0 )
        {
            numOfBlocksRemaining--;
            currentBlockOffset++;
        }

        if( ( numOfBlocksRemaining % 10 ) == 0 )
        {
        	LogInfo("Free OTA buffers %u", getFreeOTABuffers());
        }

        if( numOfBlocksRemaining == 0 )
        {
            nextEvent.eventId = OtaAgentEventCloseFile;
            OtaSendEvent_FreeRTOS( &nextEvent );
        }
        else
        {
        	if( currentBlockOffset % NUM_OF_BLOCKS_REQUESTED == 0 )
        	{
                nextEvent.eventId = OtaAgentEventRequestFileBlock;
                OtaSendEvent_FreeRTOS( &nextEvent );
        	}
        }
        break;

    case OtaAgentEventCloseFile:
        LogInfo("Close file event Received \n");
        LogInfo("-----------------------\n");
        if( closeFile() == true )
        {
        	nextEvent.eventId = OtaAgentEventActivateImage;
        	OtaSendEvent_FreeRTOS( &nextEvent );
        }
        break;

    case OtaAgentEventActivateImage:
    	LogInfo("Activate Image event Received \n");
		LogInfo("-----------------------\n");
		if( activateImage() == true )
		{
			nextEvent.eventId = OtaAgentEventActivateImage;
			OtaSendEvent_FreeRTOS( &nextEvent );
		}

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
static int16_t handleMqttStreamsBlockArrived(
    uint8_t *data, size_t dataLength )
{
	int16_t writeblockRes = -1;
    assert( ( totalBytesReceived + dataLength ) <
            CONFIG_MAX_FILE_SIZE );

    LogInfo( "Downloaded block %u of %u. \n", currentBlockOffset, (currentBlockOffset + numOfBlocksRemaining) );

    writeblockRes = otaPal_WriteBlock( &jobFields,
                               totalBytesReceived,
                               data,
							   dataLength );

    if( writeblockRes > 0 )
    {
    	totalBytesReceived += writeblockRes;
    }

    return writeblockRes;
}


static bool closeFile( void )
{
    return otaPal_CloseFile( &jobFields );
}

static bool activateImage( void )
{
	return otaPal_ActivateNewImage( &jobFields );
}


static bool sendSuccessMessage( void )
{
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
                ( uint16_t ) thingNameLength,
                globalJobId,
				( uint16_t ) strnlen( globalJobId, 1000U ),
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
