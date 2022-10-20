/*
 * FreeRTOS STM32 Reference Integration
 *
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

#include "logging_levels.h"

#define LOG_LEVEL    LOG_DEBUG

#include "logging.h"

#include "test_param_config.h"
#include "test_execution_config.h"
#include "qualification_test.h"
#include "transport_interface_test.h"
#include "ota_pal_test.h"
#include "mqtt_test.h"
/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "mbedtls_transport.h"
#include "sys_evt.h"         /* For get network ready event. */
#include "mqtt_agent_task.h" /* For device advisor test. */
#include "ota_config.h"

#define TEST_RESULT_BUFFER_CAPACITY    1024

/*----------------------- Log Helper -----------------------*/

/* The buffer to store test result. The content will be printed if an eol character
 * is received */
static char pcTestResultBuffer[ TEST_RESULT_BUFFER_CAPACITY ];
static int16_t xBufferSize = 0;

/*----------------------- Log Helper -----------------------*/

void TEST_SubmitResultBuffer();
void TEST_SubmitResult( const char * pcResult );

/*----------------------- Log Helper -----------------------*/
void TEST_CacheResult( char cResult )
{
    if( TEST_RESULT_BUFFER_CAPACITY - xBufferSize == 2 )
    {
        cResult = '\n';
    }

    pcTestResultBuffer[ xBufferSize++ ] = cResult;

    if( ( '\n' == cResult ) )
    {
        TEST_SubmitResultBuffer();
    }
}
/*----------------------- Log Helper -----------------------*/

void TEST_SubmitResultBuffer()
{
    if( 0 != xBufferSize )
    {
        TEST_SubmitResult( pcTestResultBuffer );
        memset( pcTestResultBuffer, 0, TEST_RESULT_BUFFER_CAPACITY );
        xBufferSize = 0;
    }
}
/*----------------------- Log Helper -----------------------*/

void TEST_NotifyTestStart()
{
    TEST_SubmitResult( "---------STARTING TESTS---------\n" );
}
/*----------------------- Log Helper -----------------------*/

void TEST_NotifyTestFinished()
{
    TEST_SubmitResult( "-------ALL TESTS FINISHED-------\n" );
}
/*----------------------- Log Helper -----------------------*/

void TEST_SubmitResult( const char * pcResult )
{
    /* We want to print test result no matter configPRINTF is defined or not */
    LogInfo( pcResult );

    /* Wait for 1 seconds to let print task empty its buffer. */
    vTaskDelay( pdMS_TO_TICKS( 1000UL ) );
}
/*----------------------- Log Helper -----------------------*/

/**
 * @brief Socket send and receive timeouts to use.  Specified in milliseconds.
 */
#define mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS    ( 750 )

typedef struct NetworkCrendentials
{
    PkiObject_t xPrivateKey;
    PkiObject_t xClientCertificate;
    PkiObject_t pxRootCaChain[ 1 ];
} NetworkCredentials_t;

static NetworkCredentials_t xNetworkCredentials = { 0 };
static NetworkCredentials_t xSecondNetworkCredentials = { 0 };
static TransportInterface_t xTransport = { 0 };
static NetworkContext_t * pxNetworkContext = NULL;
static NetworkContext_t * pxSecondNetworkContext = NULL;

/**
 * @brief Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the chances
 * of overflow for the 32 bit unsigned integer used for holding the timestamp.
 */
static uint32_t ulGlobalEntryTimeMs;

static NetworkConnectStatus_t prvTransportNetworkConnect( void * pvNetworkContext,
                                                          TestHostInfo_t * pxHostInfo,
                                                          void * pvNetworkCredentials )
{
    TlsTransportStatus_t xTlsStatus = TLS_TRANSPORT_SUCCESS;

    xTlsStatus = mbedtls_transport_connect( pvNetworkContext,
                                            pxHostInfo->pHostName,
                                            ( uint16_t ) pxHostInfo->port,
                                            mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS,
                                            mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS );

    configASSERT( TLS_TRANSPORT_SUCCESS == xTlsStatus );

    return NETWORK_CONNECT_SUCCESS;
}


static void prvTransportNetworkDisconnect( void * pNetworkContext )
{
    mbedtls_transport_disconnect( pNetworkContext );
}

typedef struct TaskParam
{
    StaticSemaphore_t joinMutexBuffer;
    SemaphoreHandle_t joinMutexHandle;
    FRTestThreadFunction_t threadFunc;
    void * pParam;
    TaskHandle_t taskHandle;
} TaskParam_t;

static void ThreadWrapper( void * pParam )
{
    TaskParam_t * pTaskParam = pParam;

    if( ( pTaskParam != NULL ) && ( pTaskParam->threadFunc != NULL ) && ( pTaskParam->joinMutexHandle != NULL ) )
    {
        pTaskParam->threadFunc( pTaskParam->pParam );

        /* Give the mutex. */
        xSemaphoreGive( pTaskParam->joinMutexHandle );
    }

    vTaskDelete( NULL );
}
/*-----------------------------------------------------------*/

extern UBaseType_t uxRand( void );
int FRTest_GenerateRandInt()
{
    return ( int ) uxRand();
}

/*-----------------------------------------------------------*/

FRTestThreadHandle_t FRTest_ThreadCreate( FRTestThreadFunction_t threadFunc,
                                          void * pParam )
{
    TaskParam_t * pTaskParam = NULL;
    FRTestThreadHandle_t threadHandle = NULL;
    BaseType_t xReturned;

    pTaskParam = malloc( sizeof( TaskParam_t ) );
    configASSERT( pTaskParam != NULL );

    pTaskParam->joinMutexHandle = xSemaphoreCreateBinaryStatic( &pTaskParam->joinMutexBuffer );
    configASSERT( pTaskParam->joinMutexHandle != NULL );

    pTaskParam->threadFunc = threadFunc;
    pTaskParam->pParam = pParam;

    xReturned = xTaskCreate( ThreadWrapper,    /* Task code. */
                             "ThreadWrapper",  /* All tasks have same name. */
                             8192,             /* Task stack size. */
                             pTaskParam,       /* Where the task writes its result. */
                             tskIDLE_PRIORITY, /* Task priority. */
                             &pTaskParam->taskHandle );
    configASSERT( xReturned == pdPASS );

    threadHandle = pTaskParam;

    return threadHandle;
}

/*-----------------------------------------------------------*/

int FRTest_ThreadTimedJoin( FRTestThreadHandle_t threadHandle,
                            uint32_t timeoutMs )
{
    TaskParam_t * pTaskParam = threadHandle;
    BaseType_t xReturned;
    int retValue = 0;

    /* Check the parameters. */
    configASSERT( pTaskParam != NULL );
    configASSERT( pTaskParam->joinMutexHandle != NULL );

    /* Wait for the thread. */
    xReturned = xSemaphoreTake( pTaskParam->joinMutexHandle, pdMS_TO_TICKS( timeoutMs ) );

    if( xReturned != pdTRUE )
    {
        LogWarn( ( "Waiting thread exist failed after %u %d. Task abort.", timeoutMs, xReturned ) );

        /* Return negative value to indicate error. */
        retValue = -1;

        /* There may be used after free. Assert here to indicate error. */
        configASSERT( 0 );
    }

    free( pTaskParam );

    return retValue;
}

/*-----------------------------------------------------------*/

void FRTest_TimeDelay( uint32_t delayMs )
{
    vTaskDelay( pdMS_TO_TICKS( delayMs ) );
}

/*-----------------------------------------------------------*/

void * FRTest_MemoryAlloc( size_t size )
{
    return pvPortMalloc( size );
}

/*-----------------------------------------------------------*/

void FRTest_MemoryFree( void * ptr )
{
    return vPortFree( ptr );
}
/*-----------------------------------------------------------*/

uint32_t MqttTestGetTimeMs( void )
{
    TickType_t xTickCount = 0;
    uint32_t ulTimeMs = 0UL;

    /* Get the current tick count. */
    xTickCount = xTaskGetTickCount();

    /* Convert the ticks to milliseconds. */
    ulTimeMs = ( uint32_t ) pdMS_TO_TICKS( xTickCount );

    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
     * elapsed time in the application. */
    ulTimeMs = ( uint32_t ) ( ulTimeMs - ulGlobalEntryTimeMs );

    return ulTimeMs;
}
/*-----------------------------------------------------------*/

uint32_t FRTest_GetTimeMs( void )
{
    return MqttTestGetTimeMs();
}
/*-----------------------------------------------------------*/

#if ( MQTT_TEST_ENABLED == 1 )
void SetupMqttTestParam( MqttTestParam_t * pTestParam )
{
    TlsTransportStatus_t xTlsStatus = TLS_TRANSPORT_SUCCESS;

    configASSERT( pTestParam != NULL );

    /* Initialization of timestamp for MQTT. */
    ulGlobalEntryTimeMs = MqttTestGetTimeMs();

    /* Setup the transport interface. */
    xTransport.send = mbedtls_transport_send;
    xTransport.recv = mbedtls_transport_recv;

    pxNetworkContext = mbedtls_transport_allocate();
    configASSERT( pxNetworkContext != NULL );

    xNetworkCredentials.xPrivateKey = xPkiObjectFromLabel( TLS_KEY_PRV_LABEL );
    xNetworkCredentials.xClientCertificate = xPkiObjectFromLabel( TLS_CERT_LABEL );
    xNetworkCredentials.pxRootCaChain[ 0 ] = xPkiObjectFromLabel( TLS_ROOT_CA_CERT_LABEL );

    xTlsStatus = mbedtls_transport_configure( pxNetworkContext,
                                              NULL,
                                              &xNetworkCredentials.xPrivateKey,
                                              &xNetworkCredentials.xClientCertificate,
                                              xNetworkCredentials.pxRootCaChain,
                                              1 );

    configASSERT( xTlsStatus == TLS_TRANSPORT_SUCCESS );

    pxSecondNetworkContext = mbedtls_transport_allocate();
    configASSERT( pxSecondNetworkContext != NULL );

    xSecondNetworkCredentials.xPrivateKey = xPkiObjectFromLabel( TLS_KEY_PRV_LABEL );
    xSecondNetworkCredentials.xClientCertificate = xPkiObjectFromLabel( TLS_CERT_LABEL );
    xSecondNetworkCredentials.pxRootCaChain[ 0 ] = xPkiObjectFromLabel( TLS_ROOT_CA_CERT_LABEL );

    xTlsStatus = mbedtls_transport_configure( pxSecondNetworkContext,
                                              NULL,
                                              &xSecondNetworkCredentials.xPrivateKey,
                                              &xSecondNetworkCredentials.xClientCertificate,
                                              xSecondNetworkCredentials.pxRootCaChain,
                                              1 );

    configASSERT( xTlsStatus == TLS_TRANSPORT_SUCCESS );

    pTestParam->pTransport = &xTransport;
    pTestParam->pNetworkContext = pxNetworkContext;
    pTestParam->pSecondNetworkContext = pxSecondNetworkContext;
    pTestParam->pNetworkConnect = prvTransportNetworkConnect;
    pTestParam->pNetworkDisconnect = prvTransportNetworkDisconnect;
    pTestParam->pNetworkCredentials = &xNetworkCredentials;
    pTestParam->pGetTimeMs = MqttTestGetTimeMs;
}
#endif /* TRANSPORT_INTERFACE_TEST_ENABLED == 1 */
/*-----------------------------------------------------------*/

#if ( TRANSPORT_INTERFACE_TEST_ENABLED == 1 )
void SetupTransportTestParam( TransportTestParam_t * pTestParam )
{
    TlsTransportStatus_t xTlsStatus = TLS_TRANSPORT_SUCCESS;

    configASSERT( pTestParam != NULL );

    /* Initialization of timestamp for MQTT. */
    ulGlobalEntryTimeMs = MqttTestGetTimeMs();

    /* Setup the transport interface. */
    xTransport.send = mbedtls_transport_send;
    xTransport.recv = mbedtls_transport_recv;

    pxNetworkContext = mbedtls_transport_allocate();
    configASSERT( pxNetworkContext != NULL );

    xNetworkCredentials.xPrivateKey = xPkiObjectFromLabel( TLS_KEY_PRV_LABEL );
    xNetworkCredentials.xClientCertificate = xPkiObjectFromLabel( TLS_CERT_LABEL );
    xNetworkCredentials.pxRootCaChain[ 0 ] = xPkiObjectFromLabel( TLS_ROOT_CA_CERT_LABEL );

    xTlsStatus = mbedtls_transport_configure( pxNetworkContext,
                                              NULL,
                                              &xNetworkCredentials.xPrivateKey,
                                              &xNetworkCredentials.xClientCertificate,
                                              xNetworkCredentials.pxRootCaChain,
                                              1 );

    configASSERT( xTlsStatus == TLS_TRANSPORT_SUCCESS );

    pxSecondNetworkContext = mbedtls_transport_allocate();
    configASSERT( pxSecondNetworkContext != NULL );

    xSecondNetworkCredentials.xPrivateKey = xPkiObjectFromLabel( TLS_KEY_PRV_LABEL );
    xSecondNetworkCredentials.xClientCertificate = xPkiObjectFromLabel( TLS_CERT_LABEL );
    xSecondNetworkCredentials.pxRootCaChain[ 0 ] = xPkiObjectFromLabel( TLS_ROOT_CA_CERT_LABEL );

    xTlsStatus = mbedtls_transport_configure( pxSecondNetworkContext,
                                              NULL,
                                              &xSecondNetworkCredentials.xPrivateKey,
                                              &xSecondNetworkCredentials.xClientCertificate,
                                              xSecondNetworkCredentials.pxRootCaChain,
                                              1 );

    configASSERT( xTlsStatus == TLS_TRANSPORT_SUCCESS );

    pTestParam->pTransport = &xTransport;
    pTestParam->pNetworkContext = pxNetworkContext;
    pTestParam->pSecondNetworkContext = pxSecondNetworkContext;
    pTestParam->pNetworkConnect = prvTransportNetworkConnect;
    pTestParam->pNetworkDisconnect = prvTransportNetworkDisconnect;
    pTestParam->pNetworkCredentials = &xNetworkCredentials;
}
#endif /* if ( TRANSPORT_INTERFACE_TEST_ENABLED == 1 ) */

#if ( OTA_PAL_TEST_ENABLED == 1 )
void SetupOtaPalTestParam( OtaPalTestParam_t * pTestParam )
{
    pTestParam->pageSize = 1 << otaconfigLOG2_FILE_BLOCK_SIZE;
}
#endif /* if ( OTA_PAL_TEST_ENABLED == 1 ) */
/*-----------------------------------------------------------*/

void run_qualification_main( void * pvArgs )
{
    ( void ) pvArgs;

    LogInfo( "Start qualification test." );
    LogInfo( "Waiting network connected event." );

    /* Block until the network interface is connected */
    ( void ) xEventGroupWaitBits( xSystemEvents,
                                  EVT_MASK_NET_CONNECTED,
                                  0x00,
                                  pdTRUE,
                                  portMAX_DELAY );

    LogInfo( "Run qualification test." );

    RunQualificationTest();

    LogInfo( "End qualification test." );

    for( ; ; )
    {
        vTaskDelay( pdMS_TO_TICKS( 30000UL ) );
    }

    vTaskDelete( NULL );
}
/*-----------------------------------------------------------*/

extern void vMQTTAgentTask( void * );
extern void vOTAUpdateTask( void * );
extern void vSubscribePublishTestTask( void * );

int RunDeviceAdvisorDemo( void )
{
    BaseType_t xResult;

    xResult = xTaskCreate( vMQTTAgentTask, "MQTTAgent", 2048, NULL, 10, NULL );
    configASSERT( xResult == pdTRUE );

    xResult = xTaskCreate( vSubscribePublishTestTask, "PubSub", 6144, NULL, 10, NULL );
    configASSERT( xResult == pdTRUE );

    return 0;
}
/*-----------------------------------------------------------*/

int RunOtaE2eDemo( void )
{
    BaseType_t xResult;

    xResult = xTaskCreate( vMQTTAgentTask, "MQTTAgent", 2048, NULL, 10, NULL );
    configASSERT( xResult == pdTRUE );

    xResult = xTaskCreate( vOTAUpdateTask, "OTAUpdate", 4096, NULL, tskIDLE_PRIORITY + 1, NULL );
    configASSERT( xResult == pdTRUE );

    return 0;
}
