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

#include "hw_defs.h"
#include "sys_evt.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32u5xx.h"
#include "kvstore.h"
#include "hw_defs.h"
#include "psa/crypto.h"
#include <string.h>

#include "test_execution_config.h"

#include "cli/cli.h"

/* Definition for Qualification Test */
#if ( DEVICE_ADVISOR_TEST_ENABLED == 1 ) || ( MQTT_TEST_ENABLED == 1 ) || ( TRANSPORT_INTERFACE_TEST_ENABLED == 1 ) || \
    ( OTA_PAL_TEST_ENABLED == 1 ) || ( OTA_E2E_TEST_ENABLED == 1 ) || ( CORE_PKCS11_TEST_ENABLED == 1 )
    #define DEMO_QUALIFICATION_TEST    ( 1 )

#else
    #define DEMO_QUALIFICATION_TEST    ( 0 )
#endif /* ( DEVICE_ADVISOR_TEST_ENABLED == 1 ) || ( MQTT_TEST_ENABLED == 1 ) || ( TRANSPORT_INTERFACE_TEST_ENABLED == 1 ) || \
        * ( OTA_PAL_TEST_ENABLED == 1 ) || ( OTA_E2E_TEST_ENABLED == 1 ) || ( CORE_PKCS11_TEST_ENABLED == 1 ) */

EventGroupHandle_t xSystemEvents = NULL;

typedef void ( * VectorTable_t )( void );

#define NUM_USER_IRQ               ( FMAC_IRQn + 1 )      /* MCU specific */
#define VECTOR_TABLE_SIZE          ( NVIC_USER_IRQ_OFFSET + NUM_USER_IRQ )
#define VECTOR_TABLE_ALIGN_CM33    0x400U

static VectorTable_t pulVectorTableSRAM[ VECTOR_TABLE_SIZE ] __attribute__( ( aligned( VECTOR_TABLE_ALIGN_CM33 ) ) );

extern int32_t ns_interface_lock_init( void );

/* Relocate vector table to ram for runtime interrupt registration */
static void vRelocateVectorTable( void )
{
    /* Disable interrupts */
    __disable_irq();

    HAL_ICACHE_Disable();
    HAL_DCACHE_Disable( pxHndlDCache );

    /* Copy vector table to ram */
    ( void ) memcpy( pulVectorTableSRAM, ( uint32_t * ) SCB->VTOR, sizeof( uint32_t ) * VECTOR_TABLE_SIZE );

    SCB->VTOR = ( uint32_t ) pulVectorTableSRAM;

    __DSB();
    __ISB();

    HAL_DCACHE_Invalidate( pxHndlDCache );
    HAL_ICACHE_Invalidate();
    HAL_ICACHE_Enable();
    HAL_DCACHE_Enable( pxHndlDCache );

    __enable_irq();
}


static void vHeartbeatTask( void * pvParameters )
{
    ( void ) pvParameters;

    HAL_GPIO_WritePin( LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET );
    HAL_GPIO_WritePin( LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET );

    while( 1 )
    {
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
        HAL_GPIO_TogglePin( LED_GREEN_GPIO_Port, LED_GREEN_Pin );
    }
}

extern void net_main( void * pvParameters );
extern void vMQTTAgentTask( void * );
extern void vMotionSensorsPublish( void * );
extern void vEnvironmentSensorPublishTask( void * );
extern void vShadowDeviceTask( void * );
extern void otaAgentTask( void * parameters );
extern void vDefenderAgentTask( void * );
#if DEMO_QUALIFICATION_TEST
    extern void run_qualification_main( void * );
#endif /* DEMO_QUALIFICATION_TEST */

void vInitTask( void * pvArgs )
{
    BaseType_t xResult;

    /* Initialize PSA crypto api */
    psa_crypto_init();

    xResult = xTaskCreate( Task_CLI, "cli", 2048, NULL, 10, NULL );

    ( void ) xEventGroupSetBits( xSystemEvents, EVT_MASK_FS_READY );

    KVStore_init();

    xResult = xTaskCreate( vHeartbeatTask, "Heartbeat", 128, NULL, tskIDLE_PRIORITY, NULL );
    configASSERT( xResult == pdTRUE );

    xResult = xTaskCreate( &net_main, "MxNet", 1024, NULL, 23, NULL );
    configASSERT( xResult == pdTRUE );

    #if DEMO_QUALIFICATION_TEST
        xResult = xTaskCreate( run_qualification_main, "QualTest", 4096, NULL, 10, NULL );
        configASSERT( xResult == pdTRUE );
    #else
        xResult = xTaskCreate( vMQTTAgentTask, "MQTTAgent", 2048, NULL, tskIDLE_PRIORITY + 3, NULL );
        configASSERT( xResult == pdTRUE );

        xResult = xTaskCreate( otaAgentTask, "OTAUpdate", 2048, NULL, tskIDLE_PRIORITY + 3, NULL );
        configASSERT( xResult == pdTRUE );

        xResult = xTaskCreate( vEnvironmentSensorPublishTask, "EnvSense", 1024, NULL, tskIDLE_PRIORITY + 2, NULL );
        configASSERT( xResult == pdTRUE );

        xResult = xTaskCreate( vMotionSensorsPublish, "MotionS", 1024, NULL, tskIDLE_PRIORITY + 2, NULL );
        configASSERT( xResult == pdTRUE );

        xResult = xTaskCreate( vShadowDeviceTask, "ShadowDevice", 1024, NULL, tskIDLE_PRIORITY + 1, NULL );
        configASSERT( xResult == pdTRUE );

        xResult = xTaskCreate( vDefenderAgentTask, "AWSDefender", 2048, NULL, tskIDLE_PRIORITY + 1, NULL );
        configASSERT( xResult == pdTRUE );
    #endif /* DEMO_QUALIFICATION_TEST */

    while( 1 )
    {
        vTaskSuspend( NULL );
    }
}

int main( void )
{
    hw_init();

    vRelocateVectorTable();

    vLoggingInit();

    LogInfo( "HW Init Complete." );

    if( ns_interface_lock_init() != 0 )
    {
        configASSERT( 0 );
    }

    xSystemEvents = xEventGroupCreate();

    xTaskCreate( vInitTask, "Init", 1024, NULL, 8, NULL );

    /* Start scheduler */
    vTaskStartScheduler();

    /* Initialize threads */

    LogError( "Kernel start returned." );

    /* This loop should be inaccessible.*/
    while( 1 )
    {
        __NOP();
    }
}

UBaseType_t uxRand( void )
{
    UBaseType_t uxRandVal = 0;

    if( psa_generate_random( ( uint8_t * ) ( &uxRandVal ), sizeof( UBaseType_t ) ) != PSA_SUCCESS )
    {
        configASSERT_CONTINUE( 0 );
    }

    return uxRandVal;
}

/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
 * used by the Idle task. */
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                    StackType_t ** ppxIdleTaskStackBuffer,
                                    uint32_t * pulIdleTaskStackSize )
{
    /* If the buffers to be provided to the Idle task are declared inside this
     * function then they must be declared static - otherwise they will be allocated on
     * the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
     * state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
 * application must provide an implementation of vApplicationGetTimerTaskMemory()
 * to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer,
                                     StackType_t ** ppxTimerTaskStackBuffer,
                                     uint32_t * pulTimerTaskStackSize )
{
    /* If the buffers to be provided to the Timer task are declared inside this
     * function then they must be declared static - otherwise they will be allocated on
     * the stack and so not exists after this function exits. */
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    /* Pass out a pointer to the StaticTask_t structure in which the Timer
     * task's state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task's stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}


/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
    LogError( "Malloc failed" );

    while( 1 )
    {
        __NOP();
    }
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                    char * pcTaskName )

{
    volatile uint32_t ulSetToZeroToStepOut = 1UL;

    taskENTER_CRITICAL();

    LogDebug( "Stack overflow in %s", pcTaskName );
    ( void ) xTask;
    ( void ) pcTaskName; /* Remove compiler warnings if LogDebug() is not defined. */

    while( ulSetToZeroToStepOut != 0 )
    {
        __NOP();
    }

    taskEXIT_CRITICAL();
}

/*-----------------------------------------------------------*/

#if configUSE_IDLE_HOOK == 1
    void vApplicationIdleHook( void )
    {
        /* Check / pet the watchdog */
        if( pxHwndIwdg != NULL )
        {
            HAL_IWDG_Refresh( pxHwndIwdg );
        }
    }
#endif /* configUSE_IDLE_HOOK == 1 */

/*-----------------------------------------------------------*/






#ifdef TFM_PSA_API_TEST

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




#ifdef TFM_PSA_API_TEST
            {
                /*
                 * Do version check validation here, given that OTA Agent library does not handle
                 * runtime version check of secure or non-secure images.
                 */
                if( ( prvImageVersionCheck( FWU_IMAGE_TYPE_SECURE ) == true ) &&
                    ( prvImageVersionCheck( FWU_IMAGE_TYPE_NONSECURE ) == true ) )
                {
                	result = otaPal_SetPlatformImageState( pxFileContext, OtaImageStateAccepted );
                }
                else
                {
                	result = otaPal_SetPlatformImageState( pxFileContext, OtaImageStateRejected );

                    if( result == true )
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
#endif /* ifdef TFM_PSA_API */
