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
#define LOG_LEVEL LOG_DEBUG
#include "logging.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "net/mxchip/mx_netconn.h"
#include "stm32u5xx_ll_rng.h"
#include "stm32u5xx.h"
#include "kvstore.h"

#include "cli/cli.h"
#include "lfs.h"
#include "fs/lfs_port.h"

static lfs_t * pxLfsCtx = NULL;

lfs_t * pxGetDefaultFsCtx( void )
{
    while( pxLfsCtx == NULL )
    {
        LogDebug( "Waiting for FS Initialization." );
        /* Wait for FS to be initialized */
        vTaskDelay( 1000 );
        //TODO block on an event group bit instead
    }
    return pxLfsCtx;
}

typedef void( * VectorTable_t )(void);

#define NUM_USER_IRQ				( FMAC_IRQn + 1 ) /* MCU specific */
#define VECTOR_TABLE_SIZE 		    ( NVIC_USER_IRQ_OFFSET + NUM_USER_IRQ )
#define VECTOR_TABLE_ALIGN_CM33		0x200U

static VectorTable_t pulVectorTableSRAM[ VECTOR_TABLE_SIZE ] __attribute__(( aligned (VECTOR_TABLE_ALIGN_CM33) ));

DCACHE_HandleTypeDef hDcache;

/* Relocate vector table to ram for runtime interrupt registration */
static void vRelocateVectorTable( void )
{
    /* Disable interrupts */
    __disable_irq();

//    HAL_ICACHE_Disable();
//    HAL_DCACHE_Disable( &hDcache );

    /* Copy vector table to ram */
    ( void ) memcpy( pulVectorTableSRAM, ( uint32_t * ) SCB->VTOR , sizeof( uint32_t) * VECTOR_TABLE_SIZE );

    SCB->VTOR = (uint32_t) pulVectorTableSRAM;

    __DSB();
    __ISB();

//    HAL_DCACHE_Invalidate( &hDcache );
//    HAL_ICACHE_Invalidate();
//    HAL_ICACHE_Enable();
//    HAL_DCACHE_Enable( &hDcache );

    __enable_irq();
}


/* Initialize hardware / STM32 HAL library */
static void hw_init( void )
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();


    /*
     * Initializes flash interface and systick timer.
     * Note: HAL_Init calls HAL_MspInit.
     */
    HAL_Init();
    HAL_PWREx_EnableVddIO2();

    /* System interrupt init*/
    /* PendSV_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(PendSV_IRQn, 7, 0);

    /* Configure the system clock */
    SystemClock_Config();

    /* initialize ICACHE (makes flash access faster) */
    HAL_ICACHE_ConfigAssociativityMode( ICACHE_1WAY );
    HAL_ICACHE_Invalidate();
    HAL_ICACHE_Enable();

    /* Initialize DCACHE */

    hDcache.Instance = DCACHE1;
    hDcache.Init.ReadBurstType = DCACHE_READ_BURST_WRAP;
    HAL_DCACHE_Init( &hDcache );
    HAL_DCACHE_Invalidate( &hDcache );
    HAL_DCACHE_Enable( &hDcache );


    /* Initialize uart for logging before cli is up and running */
    vInitLoggingEarly();

    /* Initialize GPIO */
    MX_GPIO_Init();

    MX_RTC_Init();

    extern SPI_HandleTypeDef hspi2;

    HAL_SPI_RegisterCallback( &hspi2, HAL_SPI_MSPINIT_CB_ID, &HAL_SPI_MspInit );

    MX_GPDMA1_Init();
    MX_SPI2_Init();

    /* Initialize crypto accelerators */
    MX_HASH_Init();
    MX_RNG_Init();
    MX_PKA_Init();
}

static int fs_init( void )
{
    static lfs_t xLfsCtx = { 0 };

    struct lfs_info xDirInfo = { 0 };

    /* Block time of up to 1 s for filesystem to initialize */
    const struct lfs_config * pxCfg = pxInitializeOSPIFlashFs( pdMS_TO_TICKS( 30 * 1000 ) );

    /* mount the filesystem */
    int err = lfs_mount( &xLfsCtx, pxCfg );

    /* format if we can't mount the filesystem
     * this should only happen on the first boot
     */
    if( err != LFS_ERR_OK )
    {
        LogError( "Failed to mount partition. Formatting..." );
        err = lfs_format( &xLfsCtx, pxCfg );
        if( err == 0 )
        {
            err = lfs_mount( &xLfsCtx, pxCfg );
        }

        if( err != LFS_ERR_OK )
        {
            LogError( "Failed to format littlefs device." );
        }
    }

    if( lfs_stat( &xLfsCtx, "/cfg", &xDirInfo ) == LFS_ERR_NOENT )
    {
        err = lfs_mkdir( &xLfsCtx, "/cfg" );

        if( err != LFS_ERR_OK )
        {
            LogError( "Failed to create /cfg directory." );
        }
    }

    if( lfs_stat( &xLfsCtx, "/ota", &xDirInfo ) == LFS_ERR_NOENT )
    {
    	err = lfs_mkdir( &xLfsCtx, "/ota" );

    	if( err != LFS_ERR_OK )
    	{
    		LogError( "Failed to create /ota directory." );
    	}
    }


    if( err == 0 )
    {
        /* Export the FS context */
        pxLfsCtx = &xLfsCtx;
    }

    return err;
}

static void vHeartbeatTask( void * pvParameters )
{
    ( void ) pvParameters;

    HAL_GPIO_WritePin( LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET );
    HAL_GPIO_WritePin( LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET );

    while(1)
    {
//        LogSys( "Idle priority heartbeat." );
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
        HAL_GPIO_TogglePin( LED_GREEN_GPIO_Port, LED_GREEN_Pin );
        HAL_GPIO_TogglePin( LED_RED_GPIO_Port, LED_RED_Pin );
    }
}

extern void vStartMQTTAgentDemo( void );
extern void Task_MotionSensorsPublish( void * );
extern void vEnvironmentSensorPublishTask( void * );
extern void vShadowDeviceTask( void * );
extern void vShadowUpdateTask( void * );
extern void vStartOTAUpdateTask( configSTACK_DEPTH_TYPE uxStackSize, UBaseType_t uxPriority );

void vInitTask( void * pvArgs )
{
    BaseType_t xResult;

    xResult = xTaskCreate( Task_CLI, "cli", 4096, NULL, 10, NULL );

    FLASH_WaitForLastOperation(1000);
    int xMountStatus = fs_init();

    if( xMountStatus == LFS_ERR_OK )
    {
    	/*
    	 * FIXME: Need to debug  the cause of internal flash status register error here.
    	 * Clearing the flash status register as a workaround.
    	 */
        FLASH_WaitForLastOperation(1000);

        LogInfo( "File System mounted." );

        KVStore_init();
    }
    else
    {
        LogError( "Failed to mount filesystem." );
    }

    FLASH_WaitForLastOperation(1000);

        xResult = xTaskCreate( vHeartbeatTask, "Heartbeat", 1024, NULL, tskIDLE_PRIORITY, NULL );

        configASSERT( xResult == pdTRUE );

        xResult = xTaskCreate( &net_main, "MxNet", 2 * 4096, NULL, 23, NULL );

        configASSERT( xResult == pdTRUE );

        vStartMQTTAgentDemo();

        vStartOTAUpdateTask( 4096, tskIDLE_PRIORITY );

        xResult = xTaskCreate( vEnvironmentSensorPublishTask, "EnvSense", 4096, NULL, 10, NULL );
        configASSERT( xResult == pdTRUE );


        xResult = xTaskCreate( Task_MotionSensorsPublish, "MotionS", 4096, NULL, 11, NULL );
        configASSERT( xResult == pdTRUE );


    //    xResult = xTaskCreate( vShadowDeviceTask, "ShadowDevice", 1024, NULL, 5, NULL );
    //    configASSERT( xResult == pdTRUE );

    //    xResult = xTaskCreate( vShadowUpdateTask, "ShadowUpdate", 1024, NULL, 5, NULL );
    //    configASSERT( xResult == pdTRUE );

    while(1)
    {
        vTaskDelay(100);
    }
}

int main( void )
{

//    vRelocateVectorTable();
	hw_init();


    vLoggingInit();

    LogInfo( "HW Init Complete." );

    xTaskCreate( vInitTask, "Init", 1024, NULL, 8, NULL );

    /* Start scheduler */
    vTaskStartScheduler();

    /* Initialize threads */

    LogError( "Kernel start returned." );

    /* This loop should be inaccessible.*/
    while(1)
    {
        __NOP();
    }
}

UBaseType_t uxRand( void )
{
    return LL_RNG_ReadRandData32( RNG_NS );
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
