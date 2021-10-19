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
#include "logging.h"

#include "FreeRTOS.h"
#include "semphr.h"

/* Standard includes. */
#include <string.h>

/* Subscription manager header include. */
#include "subscription_manager.h"

#include "core_mqtt_agent.h"

extern SubscriptionElement_t pxGlobalSubscriptionList[];

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

bool mrouter_registerCallback( const char * pcTopicFilter,
                               size_t xTopicFilterLen,
                               IncomingPubCallback_t pxCallback,
                               void * pvCtx )
{
    bool xResult = false;
    size_t xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;

    configASSERT_CONTINUE( pcTopicFilter );
    configASSERT_CONTINUE( xTopicFilterLen > 0 );
    configASSERT_CONTINUE( pxCallback );
    configASSERT_CONTINUE( pvCtx );


    if( xSemaphoreTakeRecursive( xSubMgrMutex, portMAX_DELAY ) == pdTRUE )
    {
        /* Start at end of array, so that we will insert at the first available index.
         * Scans backwards to find duplicates. */
        for( int32_t lIndex = ( uint32_t ) SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS - 1; lIndex >= 0; lIndex-- )
        {
            SubscriptionElement_t * pxSubElement = &( pxGlobalSubscriptionList[ lIndex ] );

            if( pxSubElement->usFilterStringLength == 0 )
            {
                xAvailableIndex = lIndex;
            }
            else
            {
                if( pxSubElement->usFilterStringLength == xTopicFilterLen &&
                    strncmp( pcTopicFilter, pxSubElement->pcSubscriptionFilterString, ( size_t ) xTopicFilterLen ) == 0  &&
                    pxSubElement->xTaskHandle == xTaskGetCurrentTaskHandle() &&
                    pxSubElement->pxIncomingPublishCallback == pxCallback &&
                    pxSubElement->pvIncomingPublishCallbackContext == pvCtx )
                {
                    configASSERT_CONTINUE( pdFALSE );
                    xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
                    xResult = true;
                    break;
                }
            }
        }

        if( xAvailableIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS )
        {
            pxGlobalSubscriptionList[ xAvailableIndex ].pcSubscriptionFilterString = pcTopicFilter;
            pxGlobalSubscriptionList[ xAvailableIndex ].usFilterStringLength = xTopicFilterLen;
            pxGlobalSubscriptionList[ xAvailableIndex ].pxIncomingPublishCallback = pxCallback;
            pxGlobalSubscriptionList[ xAvailableIndex ].pvIncomingPublishCallbackContext = pvCtx;
            pxGlobalSubscriptionList[ xAvailableIndex ].xTaskHandle = xTaskGetCurrentTaskHandle();
            xResult = true;
        }
        xSemaphoreGiveRecursive( xSubMgrMutex );
    }

    return xResult;
}

bool mrouter_deRegisterCallback( const char * pcTopicFilter,
                                 size_t xTopicFilterLen,
                                 IncomingPubCallback_t pxCallback,
                                 void * pvCtx )
{
    bool xSuccess = false;

    configASSERT_CONTINUE( pcTopicFilter );
    configASSERT_CONTINUE( xTopicFilterLen > 0 );
    configASSERT_CONTINUE( pxCallback );
    configASSERT_CONTINUE( pvCtx );
    configASSERT_CONTINUE( pxGlobalSubscriptionList );

    if( pcTopicFilter == NULL ||
        xTopicFilterLen == 0U ||
        pxCallback == NULL ||
        pvCtx == NULL )
    {
        xSuccess = false;
    }
    else
    {
        if( xSemaphoreTakeRecursive( xSubMgrMutex, portMAX_DELAY ) == pdTRUE )
        {
            for( uint32_t ulIndex = 0U; ulIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; ulIndex++ )
            {
                if( pxGlobalSubscriptionList[ ulIndex ].usFilterStringLength == xTopicFilterLen &&
                    strncmp( pxGlobalSubscriptionList[ ulIndex ].pcSubscriptionFilterString, pcTopicFilter, xTopicFilterLen ) == 0 &&
                    pxGlobalSubscriptionList[ ulIndex ].xTaskHandle == xTaskGetCurrentTaskHandle() &&
                    pxGlobalSubscriptionList[ ulIndex ].pxIncomingPublishCallback == pxCallback )
                {
                    memset( &( pxGlobalSubscriptionList[ ulIndex ] ), 0x00, sizeof( SubscriptionElement_t ) );
                    xSuccess = true;
                }
            }
            xSemaphoreGiveRecursive( xSubMgrMutex );
        }
    }
    return xSuccess;
}

bool submgr_addSubscription( SubscriptionElement_t * pxSubscriptionList,
                             const char * pcTopicFilterString,
                             uint16_t usTopicFilterLength,
                             IncomingPubCallback_t pxIncomingPublishCallback,
                             void * pvIncomingPublishCallbackContext )
{
    int32_t lIndex = 0;
    size_t xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
    bool xSuccess = false;

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
        xSuccess = false;
    }
    else
    {
        if( xSemaphoreTakeRecursive( xSubMgrMutex, portMAX_DELAY ) == pdTRUE )
        {
            /* Start at end of array, so that we will insert at the first available index.
             * Scans backwards to find duplicates. */
            for( lIndex = ( int32_t ) SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS - 1; lIndex >= 0; lIndex-- )
            {
                SubscriptionElement_t * pxSubElement = &( pxSubscriptionList[ lIndex ] );

                if( pxSubElement->usFilterStringLength == 0 )
                {
                    xAvailableIndex = lIndex;
                }
                else
                {
                    if( ( pxSubElement->usFilterStringLength == usTopicFilterLength ) &&
                         ( strncmp( pcTopicFilterString, pxSubElement->pcSubscriptionFilterString, ( size_t ) usTopicFilterLength ) == 0 ) &&
                         ( pxSubElement->xTaskHandle == xTaskGetCurrentTaskHandle() ) &&
                         ( pxSubElement->pxIncomingPublishCallback == pxIncomingPublishCallback ) &&
                         ( pxSubElement->pvIncomingPublishCallbackContext == pvIncomingPublishCallbackContext ) )
                    {
                        LogWarn( ( "Subscription already exists.\n" ) );
                        xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
                        xSuccess = true;
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
                pxSubscriptionList[ xAvailableIndex ].xTaskHandle = xTaskGetCurrentTaskHandle();
                xSuccess = true;
            }
            xSemaphoreGiveRecursive( xSubMgrMutex );
        }
    }

    return xSuccess;
}

/*-----------------------------------------------------------*/

bool submgr_removeSubscription( SubscriptionElement_t * pxSubscriptionList,
                                const char * pcTopicFilterString,
                                uint16_t usTopicFilterLength )
{
    uint32_t ulIndex = 0;
    bool xSuccess = false;

    if( ( pxSubscriptionList == NULL ) ||
        ( pcTopicFilterString == NULL ) ||
        ( usTopicFilterLength == 0U ) )
    {
        LogError( ( "Invalid parameter. pxSubscriptionList=%p, pcTopicFilterString=%p,"
                    " usTopicFilterLength=%u.",
                    pxSubscriptionList,
                    pcTopicFilterString,
                    ( unsigned int ) usTopicFilterLength ) );
        xSuccess = false;
    }
    else
    {
        if( xSemaphoreTakeRecursive( xSubMgrMutex, portMAX_DELAY ) == pdTRUE )
        {
            for( ulIndex = 0U; ulIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; ulIndex++ )
            {
                if( pxSubscriptionList[ ulIndex ].usFilterStringLength == usTopicFilterLength &&
                    strncmp( pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString, pcTopicFilterString, usTopicFilterLength ) == 0 &&
                    pxSubscriptionList[ ulIndex ].xTaskHandle == xTaskGetCurrentTaskHandle() )
                {
                    memset( &( pxSubscriptionList[ ulIndex ] ), 0x00, sizeof( SubscriptionElement_t ) );
                    xSuccess = true;
                }
            }
            xSemaphoreGiveRecursive( xSubMgrMutex );
        }
    }
    return xSuccess;
}

/*-----------------------------------------------------------*/

bool submgr_handleIncomingPublish( SubscriptionElement_t * pxSubscriptionList,
                                   MQTTPublishInfo_t * pxPublishInfo )
{
    uint32_t ulIndex = 0;
    bool xSuccess = false;

    if( ( pxSubscriptionList == NULL ) ||
        ( pxPublishInfo == NULL ) )
    {
        LogError( ( "Invalid parameter. pxSubscriptionList=%p, pxPublishInfo=%p,",
                    pxSubscriptionList,
                    pxPublishInfo ) );
        xSuccess = false;
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
                        xSuccess = true;
                    }
                }
            }
            xSemaphoreGiveRecursive( xSubMgrMutex );
        }
    }

    return xSuccess;
}
