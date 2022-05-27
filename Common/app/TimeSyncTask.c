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

#include "logging_levels.h"
#define LOG_LEVEL LOG_DEBUG
#include "logging.h"

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"

#include "clock_config.h"

/* C Standard library includes */
#include <stdbool.h>
#include <time.h>
#include <string.h>



#include "core_http_client.h"

#include "mbedtls_transport.h"
#include "sys_evt.h"
#include "kvstore.h"


#define TIME_SYNC_INTERVAL_S        ( 60UL ) /* Sync time every 8 hours */
#define REQUEST_HEADER_BUF_LEN      ( 256U )
#define RESPONSE_BUF_LEN            ( 1024U )
#define ARRAY_COUNT( x )            ( sizeof( x ) / sizeof( x[ 0 ] ) )
#define HOSTNAME_MAX_LEN            ( 255U )
#define HTTP_REQUEST_TIMEOUT_MS     ( 60U * 1000U )
#define HTTPS_REQUEST_PORT          ( 443U )
#define MAX_TIME_DRIFT              ( TIME_SYNC_INTERVAL_S * 2 )

typedef struct HttpRequestCtx
{
    HTTPRequestInfo_t xInfo;
    HTTPRequestHeaders_t xHeaders;
    HTTPResponse_t xResponse;
    NetworkContext_t * pxNetCtx;
} HttpRequestCtx_t;

extern char * strptime(const char *buf, const char *fmt, struct tm *tm);

/*
 * Parse a time stamp from a returned http header
 *
 * Defined in RFC 7231 Section 7.1.1.1
 *
 */
static time_t xParseDateRFC7231( const char * pcTimeStamp )
{
    time_t xTime = TIME_T_INVALID;

    const char * const ppcFormats[ 3 ] =
    {
        "%a, %d %b %Y %H:%M:%S", /* Sun, 06 Nov 1994 08:49:37 GMT    ( RFC 5322 )    */
        "%a, %d-%b-%y %H:%M:%S", /* Sunday, 06-Nov-94 08:49:37 GMT   ( RFC 850 )     */
        "%a %b %n %d %H:%M:%S %Y"   /* Sun Nov  6 08:49:37 1994         ( C asctime )   */
    };

    if( pcTimeStamp == NULL )
    {
        LogError( "pcTimeStamp must not be NULL." );
    }
    else
    {
        for( uint32_t i = 0; i < ARRAY_COUNT( ppcFormats ); i++ )
        {
            struct tm xTimeDate = { 0 };

            if( strptime( pcTimeStamp, ppcFormats[ i ], &xTimeDate ) != NULL )
            {
                /* Convert struct tm to time_t */
                xTime = mktime( &xTimeDate );

                if( xTime > 0 )
                {
                    break;
                }
                else
                {
                    xTime = TIME_T_INVALID;
                }
            }
        }
    }
    return xTime;
}

static HTTPStatus_t xInitRequestCtx( HttpRequestCtx_t * pxHttpCtx,
                                     NetworkContext_t * pxNetCtx,
                                     uint8_t * pucRsqtHdrBuffer,
                                     uint8_t * pucRespBuffer,
                                     const char * pcHostname )
{
    HTTPStatus_t xStatus = HTTPSuccess;

    configASSERT( pxHttpCtx );
    configASSERT( pxNetCtx );
    configASSERT( pucRsqtHdrBuffer );
    configASSERT( pucRespBuffer );
    configASSERT( pcHostname );

    pxHttpCtx->pxNetCtx = pxNetCtx;

    pxHttpCtx->xHeaders.headersLen = 0;

    pxHttpCtx->xHeaders.pBuffer = pucRsqtHdrBuffer;
    pxHttpCtx->xHeaders.bufferLen = REQUEST_HEADER_BUF_LEN;

    pxHttpCtx->xResponse.pBuffer = pucRespBuffer;
    pxHttpCtx->xResponse.bufferLen = RESPONSE_BUF_LEN;

    /* Initialize request info object */
    pxHttpCtx->xInfo.pMethod = HTTP_METHOD_GET;
    pxHttpCtx->xInfo.methodLen = strlen( HTTP_METHOD_GET );
    pxHttpCtx->xInfo.reqFlags = 0;

    pxHttpCtx->xInfo.pHost = pcHostname;
    pxHttpCtx->xInfo.hostLen = strnlen( pcHostname, HOSTNAME_MAX_LEN );

    pxHttpCtx->xInfo.pPath = NULL;
    pxHttpCtx->xInfo.pathLen = 0;

    xStatus = HTTPClient_InitializeRequestHeaders( &( pxHttpCtx->xHeaders ),
                                                   &( pxHttpCtx->xInfo ) );

    return xStatus;
}

static time_t xGetHttpNetworkTime( HttpRequestCtx_t * pxRequestCtx )
{
    time_t xTimeFromNetwork = TIME_T_INVALID;

    configASSERT( pxRequestCtx );
    configASSERT( pxRequestCtx->pxNetCtx );

    /* Connect TLS / socket */

    if( mbedtls_transport_connect( pxRequestCtx->pxNetCtx,
                                   pxRequestCtx->xInfo.pHost,
                                   HTTPS_REQUEST_PORT,
                                   HTTP_REQUEST_TIMEOUT_MS,
                                   HTTP_REQUEST_TIMEOUT_MS ) == TLS_TRANSPORT_SUCCESS )
    {
        HTTPStatus_t xStatus = HTTPSuccess;
        const char * pcTimestamp = NULL;
        size_t uxTimestampLen = 0;

        TransportInterface_t xTransport = {
            .pNetworkContext = pxRequestCtx->pxNetCtx,
            .recv = mbedtls_transport_recv,
            .send = mbedtls_transport_send,
        };

        xStatus = HTTPClient_Send( &xTransport,
                                   &( pxRequestCtx->xHeaders ),
                                   NULL, 0,
                                   &( pxRequestCtx->xResponse ), 0 );

        mbedtls_transport_disconnect( pxRequestCtx->pxNetCtx );

        /* Parse if request was successful */
        if( xStatus == HTTPSuccess )
        {
            xStatus = HTTPClient_ReadHeader( &( pxRequestCtx->xResponse ),
                                             "date",
                                             strlen( "date" ),
                                             &pcTimestamp,
                                             &uxTimestampLen );
            if( uxTimestampLen == 0 ||
                pcTimestamp == NULL )
            {
                xStatus = HTTPHeaderNotFound;
            }
        }

        /* If request was successful, parse the received header */
        if( xStatus == HTTPSuccess )
        {
            /* Remove \r\n characters from response buffer */
            for( size_t uxIdx = 0; uxIdx < pxRequestCtx->xResponse.bufferLen; uxIdx++ )
            {
                if( pxRequestCtx->xResponse.pBuffer[ uxIdx ] == '\r' ||
                    pxRequestCtx->xResponse.pBuffer[ uxIdx ] == '\n' )
                {
                    pxRequestCtx->xResponse.pBuffer[ uxIdx ] = '\00';
                }
            }
            xTimeFromNetwork = xParseDateRFC7231( pcTimestamp );
            LogDebug( "Got header: \"%.*s\", time: %lld", uxTimestampLen, pcTimestamp, ( uint64_t ) xTimeFromNetwork );
        }
        else
        {
            xTimeFromNetwork = TIME_T_INVALID;
        }
    }
    return xTimeFromNetwork;
}

static time_t xGetTime( clockid_t xClockId )
{
    struct timespec xTimeSpec = { 0 };

    if( clock_gettime( xClockId, &xTimeSpec ) != 0 )
    {
        xTimeSpec.tv_sec = TIME_T_INVALID;
    }
    else
    {
        /* Round seconds based on nanoseconds count */
        if( xTimeSpec.tv_nsec > ( NANOSECONDS_PER_SECOND / 2 ) )
        {
            xTimeSpec.tv_sec++;
        }
    }

    return xTimeSpec.tv_sec;
}

static BaseType_t xSetTime( clockid_t xClockId, time_t xTime )
{
    int lRslt = 0;

    struct timespec xTimeSpec;
    xTimeSpec.tv_nsec = 0;
    xTimeSpec.tv_sec = xTime;

    lRslt = clock_settime( xClockId, &xTimeSpec );

    return ( lRslt == 0 );
}

void vTimeSyncTask( void * pvArgs )
{
    char * pcHostname = NULL;
    BaseType_t xExitFlag = pdFALSE;
    NetworkContext_t * pxNetworkContext = NULL;
    HttpRequestCtx_t xRqstCtx = { 0 };
    uint8_t pucRsqtHdrBuffer[ REQUEST_HEADER_BUF_LEN ] = { 0 };
    uint8_t pucRespBuffer[ RESPONSE_BUF_LEN ] = { 0 };

    pcHostname = KVStore_getStringHeap( CS_CORE_MQTT_ENDPOINT, NULL );

    if( pcHostname == NULL )
    {
        xExitFlag = pdTRUE;
    }

    /* Initialize the TLS context */
    if( xExitFlag == pdFALSE )
    {
        PkiObject_t xRootCaCert = xPkiObjectFromLabel( TLS_ROOT_CA_CERT_LABEL );

        pxNetworkContext = mbedtls_transport_allocate();

        if( pxNetworkContext == NULL )
        {
            xExitFlag = pdTRUE;
        }
        else
        {
            TlsTransportStatus_t xTlsStatus;

            /* Client certificate authentication is not required to receive a time stamp */
            xTlsStatus = mbedtls_transport_configure( pxNetworkContext,
                                                      NULL, NULL, NULL,
                                                      &xRootCaCert, 1 );

            xExitFlag = ( xTlsStatus != TLS_TRANSPORT_SUCCESS );
        }
    }

    /* Initialize the HttpRequestCtx_t */
    if( xExitFlag == pdFALSE )
    {
        xExitFlag = ( xInitRequestCtx( &xRqstCtx,
                                       pxNetworkContext,
                                       pucRsqtHdrBuffer,
                                       pucRespBuffer,
                                       pcHostname ) != HTTPSuccess );
    }

    while( xExitFlag == pdFALSE )
    {
        TimeOut_t xTimeOut = { 0 };
        TickType_t xTicksToWait = 0;

        time_t xTimeFromRTC = TIME_T_INVALID;
        time_t xTimeFromNetwork = TIME_T_INVALID;
        time_t xTimeHighWaterMark = TIME_T_INVALID;
        time_t xSelectedTime = TIME_T_INVALID;

        /* Block until the network interface is connected */
        ( void ) xEventGroupWaitBits( xSystemEvents,
                                      EVT_MASK_NET_CONNECTED,
                                      0x00,
                                      pdTRUE,
                                      portMAX_DELAY );

        vTaskSetTimeOutState( &xTimeOut );

        xTimeFromNetwork = xGetHttpNetworkTime( &xRqstCtx );

        xTimeFromRTC = xGetTime( CLOCK_REALTIME );

        xTimeHighWaterMark = xGetTime( CLOCK_HWM );

        /* Determine the preferred time source prior to taking into account network time */
        if( xTimeFromRTC != TIME_T_INVALID &&
            xTimeHighWaterMark != TIME_T_INVALID )
        {
           /* Check if Rtc time > HWM time */
           if( xTimeFromRTC > xTimeHighWaterMark )
           {
               xSelectedTime = xTimeFromRTC;
           }
           else
           {
               xSelectedTime = xTimeHighWaterMark;
           }
        }
        else if( xTimeFromRTC != TIME_T_INVALID )
        {
            xSelectedTime = xTimeFromRTC;
        }
        else if( xTimeHighWaterMark != TIME_T_INVALID )
        {
            xSelectedTime = xTimeHighWaterMark;
        }
        else
        {
            LogWarn( "Failed to find a valid time source." );
            xSelectedTime = TIME_T_INVALID;
        }

        /* Validate network time */
        if( xTimeFromNetwork != TIME_T_INVALID )
        {
            /* Validate network time */
            if( xTimeFromNetwork < MIN_SECONDS_SINCE_1970 ||
                xTimeFromNetwork > MAX_SECONDS_SINCE_1970 )
            {
                xTimeFromNetwork = TIME_T_INVALID;
            }
            else if( xSelectedTime == TIME_T_INVALID ||
                     xSelectedTime < ( xTimeFromNetwork + MAX_TIME_DRIFT ) )
            {
                xSelectedTime = xTimeFromNetwork;
            }
            else
            {
                LogWarn( "Ignoring network time because the received time delta is larger than MAX_TIME_DRIFT." );
            }
        }

        if( xSelectedTime != TIME_T_INVALID )
        {
            if( xSetTime( CLOCK_REALTIME, xSelectedTime ) == pdFALSE )
            {
                LogError( "Failed to set RTC time to: %"PRIu64, ( uint64_t ) xSelectedTime );
            }
            else
            {
                LogInfo( "Adjusted RTC time to: %"PRIu64, ( uint64_t ) xSelectedTime );
            }

            if( xSetTime( CLOCK_HWM, xSelectedTime ) == pdFALSE )
            {
                LogError( "Failed to update time high water mark to: %"PRIu64, ( uint64_t ) xSelectedTime );
            }
            else
            {
                LogInfo( "Set time high water mark to: %"PRIu64, ( uint64_t ) xSelectedTime );
            }
        }

        xTicksToWait = ( 1000UL * TIME_SYNC_INTERVAL_S );

        LogInfo( "Waiting %lu ticks until the next time sync.", xTicksToWait );

        /* Delay until the next time sync event. */
        while( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE )
        {
            if( xExitFlag )
            {
                break;
            }
            vTaskDelay( xTicksToWait );
        }
    }

    LogInfo( "TimeSyncTask ending." );

    if( pcHostname != NULL )
    {
        vPortFree( pcHostname );
    }

    vTaskDelete( NULL );
}
