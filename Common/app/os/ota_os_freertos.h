/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

/**
 * @file ota_os_freertos.h
 * @brief Function declarations for the example OTA OS Functional interface for
 * FreeRTOS.
 */

#ifndef _OTA_OS_FREERTOS_H_
#define _OTA_OS_FREERTOS_H_

/* Standard library include. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * @ingroup ota_enum_types
 * @brief The OTA OS interface return status.
 */
typedef enum OtaOsStatus
{
    OtaOsSuccess = 0, /*!< @brief OTA OS interface success. */
    OtaOsEventQueueCreateFailed = 0x80U, /*!< @brief Failed to create the event
                                            queue. */
    OtaOsEventQueueSendFailed,    /*!< @brief Posting event message to the event
                                     queue failed. */
    OtaOsEventQueueReceiveFailed, /*!< @brief Failed to receive from the event
                                     queue. */
    OtaOsEventQueueDeleteFailed,  /*!< @brief Failed to delete the event queue.
                                   */
} OtaOsStatus_t;

/**
 * @brief Initialize the OTA events.
 *
 * This function initializes the OTA events mechanism for freeRTOS platforms.
 *
 * @param[pEventCtx]     Pointer to the OTA event context.
 *
 * @return               OtaOsStatus_t, OtaOsSuccess if success , other error
 * code on failure.
 */
OtaOsStatus_t OtaInitEvent_FreeRTOS();

/**
 * @brief Sends an OTA event.
 *
 * This function sends an event to OTA library event handler on FreeRTOS
 * platforms.
 *
 * @param[pEventCtx]     Pointer to the OTA event context.
 *
 * @param[pEventMsg]     Event to be sent to the OTA handler.
 *
 * @param[timeout]       The maximum amount of time (msec) the task should
 * block.
 *
 * @return               OtaOsStatus_t, OtaOsSuccess if success , other error
 * code on failure.
 */
OtaOsStatus_t OtaSendEvent_FreeRTOS( const void * pEventMsg );

/**
 * @brief Receive an OTA event.
 *
 * This function receives next event from the pending OTA events on FreeRTOS
 * platforms.
 *
 * @param[pEventCtx]     Pointer to the OTA event context.
 *
 * @param[pEventMsg]     Pointer to store message.
 *
 * @param[timeout]       The maximum amount of time the task should block.
 *
 * @return               OtaOsStatus_t, OtaOsSuccess if success , other error
 * code on failure.
 */
OtaOsStatus_t OtaReceiveEvent_FreeRTOS( void * pEventMsg );

/**
 * @brief Deinitialize the OTA Events mechanism.
 *
 * This function deinitialize the OTA events mechanism and frees any resources
 * used on FreeRTOS platforms.
 *
 * @param[pEventCtx]     Pointer to the OTA event context.
 *
 * @return               OtaOsStatus_t, OtaOsSuccess if success , other error
 * code on failure.
 */
void OtaDeinitEvent_FreeRTOS();

#endif /* ifndef _OTA_OS_FREERTOS_H_ */
