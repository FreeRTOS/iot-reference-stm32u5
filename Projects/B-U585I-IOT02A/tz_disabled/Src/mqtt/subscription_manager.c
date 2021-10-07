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
 * @file subscription_manager.c
 * @brief Functions for managing MQTT subscriptions.
 */


#include "logging_levels.h"
/* define LOG_LEVEL here if you want to modify the logging level from the default */

/* Remove extra C89 style parentheses */
#define LOGGING_REMOVE_PARENS

#include "logging.h"

#include "FreeRTOS.h"
#include "semphr.h"

/* Standard includes. */
#include <string.h>

/* Subscription manager header include. */
#include "subscription_manager.h"

static SemaphoreHandle_t xSubMgrMutex = NULL;

void submgr_init( void )
{
    if( xSubMgrMutex != NULL )
    {
        vSemaphoreDelete( xSubMgrMutex );
        xSubMgrMutex = NULL;
    }

    xSubMgrMutex = xSemaphoreCreateRecursiveMutex();

    configASSERT( xSubMgrMutex != NULL );

    xSemaphoreGiveRecursive( xSubMgrMutex );
}

MQTTStatus_t submgr_addSubscription( SubscriptionElement_t * pxSubscriptionList,
                             const char * pcTopicFilterString,
                             uint16_t usTopicFilterLength,
                             IncomingPubCallback_t pxIncomingPublishCallback,
                             void * pvIncomingPublishCallbackContext )
{
    int32_t lIndex = 0;
    size_t xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
    MQTTStatus_t xStatus = MQTTNoDataAvailable;

    if( ( pxSubscriptionList == NULL ) ||
        ( pcTopicFilterString == NULL ) ||
        ( usTopicFilterLength == 0U ) ||
        ( pxIncomingPublishCallback == NULL ) )
    {
        LogError( ( "Invalid parameter. pxSubscriptionList=%p, pcTopicFilterString=%p,"
                    " usTopicFilterLength=%u, pxIncomingPublishCallback=%p.",
                    pxSubscriptionList,
                    pcTopicFilterString,
                    ( unsigned int ) usTopicFilterLength,
                    pxIncomingPublishCallback ) );
        xStatus = MQTTBadParameter;
    }
    else
    {
        if( xSemaphoreTakeRecursive( xSubMgrMutex, portMAX_DELAY ) == pdTRUE )
        {
            /* Start at end of array, so that we will insert at the first available index.
             * Scans backwards to find duplicates. */
            for( lIndex = ( int32_t ) SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS - 1; lIndex >= 0; lIndex-- )
            {
                if( pxSubscriptionList[ lIndex ].usFilterStringLength == 0 )
                {
                    xAvailableIndex = lIndex;
                }
                else if( ( pxSubscriptionList[ lIndex ].usFilterStringLength == usTopicFilterLength ) &&
                         ( strncmp( pcTopicFilterString, pxSubscriptionList[ lIndex ].pcSubscriptionFilterString, ( size_t ) usTopicFilterLength ) == 0 ) )
                {
                    /* If a subscription already exists, don't do anything. */
                    if( ( pxSubscriptionList[ lIndex ].pxIncomingPublishCallback == pxIncomingPublishCallback ) &&
                        ( pxSubscriptionList[ lIndex ].pvIncomingPublishCallbackContext == pvIncomingPublishCallbackContext ) )
                    {
                        LogWarn( ( "Subscription already exists.\n" ) );
                        xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
                        xStatus = MQTTSuccess;
                        break;
                    }
                }
            }

            if( xAvailableIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS )
            {
                pxSubscriptionList[ xAvailableIndex ].pcSubscriptionFilterString = pcTopicFilterString;
                pxSubscriptionList[ xAvailableIndex ].usFilterStringLength = usTopicFilterLength;
                pxSubscriptionList[ xAvailableIndex ].pxIncomingPublishCallback = pxIncomingPublishCallback;
                pxSubscriptionList[ xAvailableIndex ].pvIncomingPublishCallbackContext = pvIncomingPublishCallbackContext;
                xStatus = MQTTSuccess;
            }
            xSemaphoreGiveRecursive( xSubMgrMutex );
        }
    }

    return xStatus;
}

/*-----------------------------------------------------------*/

MQTTStatus_t submgr_removeSubscription( SubscriptionElement_t * pxSubscriptionList,
                                        const char * pcTopicFilterString,
                                        uint16_t usTopicFilterLength )
{
    uint32_t ulIndex = 0;
    MQTTStatus_t xStatus = MQTTNoDataAvailable;

    if( ( pxSubscriptionList == NULL ) ||
        ( pcTopicFilterString == NULL ) ||
        ( usTopicFilterLength == 0U ) )
    {
        LogError( ( "Invalid parameter. pxSubscriptionList=%p, pcTopicFilterString=%p,"
                    " usTopicFilterLength=%u.",
                    pxSubscriptionList,
                    pcTopicFilterString,
                    ( unsigned int ) usTopicFilterLength ) );
        xStatus = MQTTBadParameter;
    }
    else
    {
        if( xSemaphoreTakeRecursive( xSubMgrMutex, portMAX_DELAY ) == pdTRUE )
        {
            for( ulIndex = 0U; ulIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; ulIndex++ )
            {
                if( pxSubscriptionList[ ulIndex ].usFilterStringLength == usTopicFilterLength )
                {
                    if( strncmp( pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString, pcTopicFilterString, usTopicFilterLength ) == 0 )
                    {
                        memset( &( pxSubscriptionList[ ulIndex ] ), 0x00, sizeof( SubscriptionElement_t ) );
                        xStatus = MQTTSuccess;
                    }
                }
            }
            xSemaphoreGiveRecursive( xSubMgrMutex );
        }
    }
    return xStatus;
}

/*-----------------------------------------------------------*/

MQTTStatus_t submgr_handleIncomingPublish( SubscriptionElement_t * pxSubscriptionList,
                                           MQTTPublishInfo_t * pxPublishInfo )
{
    uint32_t ulIndex = 0;
    MQTTStatus_t xStatus = MQTTNoDataAvailable;

    if( ( pxSubscriptionList == NULL ) ||
        ( pxPublishInfo == NULL ) )
    {
        LogError( ( "Invalid parameter. pxSubscriptionList=%p, pxPublishInfo=%p,",
                    pxSubscriptionList,
                    pxPublishInfo ) );
        xStatus = MQTTBadParameter;
    }
    else
    {
        if( xSemaphoreTakeRecursive( xSubMgrMutex, portMAX_DELAY ) == pdTRUE )
        {
            for( ulIndex = 0U; ulIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; ulIndex++ )
            {
                if( pxSubscriptionList[ ulIndex ].usFilterStringLength > 0 )
                {
                    bool isMatched = false;

                    MQTT_MatchTopic( pxPublishInfo->pTopicName,
                                     pxPublishInfo->topicNameLength,
                                     pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString,
                                     pxSubscriptionList[ ulIndex ].usFilterStringLength,
                                     &isMatched );

                    if( isMatched == true )
                    {
                        pxSubscriptionList[ ulIndex ].pxIncomingPublishCallback( pxSubscriptionList[ ulIndex ].pvIncomingPublishCallbackContext,
                                                                                 pxPublishInfo );
                        xStatus = MQTTSuccess;
                    }
                }
            }
            xSemaphoreGiveRecursive( xSubMgrMutex );
        }
    }

    return xStatus;
}
