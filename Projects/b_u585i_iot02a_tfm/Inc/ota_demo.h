/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#ifndef OTA_DEMO_H
#define OTA_DEMO_H

#include <stdint.h>
#include <stdbool.h>

#define OTA_DATA_BLOCK_SIZE 256U
#define JOB_DOC_SIZE 2048U

typedef enum OtaEvent
{
    OtaAgentEventStart = 0,           /*!< @brief Start the OTA state machine */
    OtaAgentEventRequestJobDocument,  /*!< @brief Event for requesting job document. */
    OtaAgentEventReceivedJobDocument, /*!< @brief Event when job document is received. */
    OtaAgentEventCreateFile,          /*!< @brief Event to create a file. */
    OtaAgentEventRequestFileBlock,    /*!< @brief Event to request file blocks. */
    OtaAgentEventReceivedFileBlock,   /*!< @brief Event to trigger when file block is received. */
    OtaAgentEventCloseFile,           /*!< @brief Event to trigger closing file. */
	OtaAgentEventActivateImage,       /*!< @brief Event to trigger activation of the image. */
    OtaAgentEventSuspend,             /*!< @brief Event to suspend ota task */
    OtaAgentEventResume,              /*!< @brief Event to resume suspended task */
    OtaAgentEventUserAbort,           /*!< @brief Event triggered by user to stop agent. */
    OtaAgentEventShutdown,            /*!< @brief Event to trigger ota shutdown */
    OtaAgentEventMax                  /*!< @brief Last event specifier */
} OtaEvent_t;

/**
 * @brief OTA Agent states.
 *
 * The current state of the OTA Task (OTA Agent).
 */
typedef enum OtaState
{
    OtaAgentStateNoTransition = -1,
    OtaAgentStateInit = 0,
    OtaAgentStateReady,
    OtaAgentStateRequestingJob,
    OtaAgentStateWaitingForJob,
    OtaAgentStateCreatingFile,
    OtaAgentStateRequestingFileBlock,
    OtaAgentStateWaitingForFileBlock,
    OtaAgentStateClosingFile,
    OtaAgentStateSuspended,
    OtaAgentStateShuttingDown,
    OtaAgentStateStopped,
    OtaAgentStateAll
} OtaState_t;

/**
 * @brief  The OTA Agent event and data structures.
 */

typedef struct OtaDataEvent
{
    uint8_t data[ OTA_DATA_BLOCK_SIZE * 2 ]; /*!< Buffer for storing event information. */
    size_t dataLength;                 /*!< Total space required for the event. */
    bool bufferUsed;                     /*!< Flag set when buffer is used otherwise cleared. */
} OtaDataEvent_t;

typedef struct OtaJobEventData
{
    uint8_t jobData[JOB_DOC_SIZE];
    size_t jobDataLength;
} OtaJobEventData_t;

/**
 * @brief Stores information about the event message.
 *
 */
typedef struct OtaEventMsg
{
    OtaDataEvent_t * dataEvent; /*!< Data Event message. */
    OtaJobEventData_t * jobEvent; /*!< Job Event message. */
    OtaEvent_t eventId;          /*!< Identifier for the event. */
} OtaEventMsg_t;


void otaDemo_start( void );

bool otaDemo_handleIncomingMQTTMessage( char * topic,
                                        size_t topicLength,
                                        uint8_t * message,
                                        size_t messageLength );

OtaState_t getOtaAgentState();
#endif
