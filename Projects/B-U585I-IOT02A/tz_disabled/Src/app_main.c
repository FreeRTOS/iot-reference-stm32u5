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
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#include "main.h"
#include "logging.h"
#include "FreeRTOS.h"
#include "task.h"
#include "network_thread.h"
#include "stm32u5xx_ll_rng.h"

/* Initialize hardware / STM32 HAL library */
static void hw_init( void )
{
	/* ???? */
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

	/* initialize ICACHE peripheral (makes flash access faster) */
	MX_ICACHE_Init();

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

static void testTask( void * pvParameters )
{
	( void ) pvParameters;
	while(1)
	{
		LogInfo(("30 seconds has elapsed"));
		vTaskDelay( pdMS_TO_TICKS( 30 * 1000 ) );
	}
}

int main( void )
{
	hw_init();

	vLoggingInit();

	LogInfo(("HW Init Complete."));

	BaseType_t xResult;

	// TODO: Add an "init" task to manage provisioning state and network state and start/stop daemons when necessary.

    /* Initialize threads */

	xResult = xTaskCreate( testTask, "testTask", 1024, NULL, tskIDLE_PRIORITY + 1, NULL );

	configASSERT( xResult == pdTRUE );
    //TODO

	xResult = xTaskCreate( &net_main, "net_main", 2 * 4096, NULL, 23, NULL );

    configASSERT( xResult == pdTRUE );

//    vStart\AgentDemo();

    /* Start scheduler */
    vTaskStartScheduler();

    LogError(("Kernel start returned."));

	/* This loop should be inaccessible.*/
	while(1)
	{

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
    LogError( ( "Malloc failed\n" ) );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                    char * pcTaskName )

{
    volatile uint32_t ulSetToZeroToStepOut = 1UL;

    taskENTER_CRITICAL();

    LogDebug( ( "Stack overflow in %s\n", pcTaskName ) );
    ( void ) xTask;
    ( void ) pcTaskName; /* Remove compiler warnings if LogDebug() is not defined. */

    while( ulSetToZeroToStepOut != 0 )
    {
    }

    taskEXIT_CRITICAL();
}
/*-----------------------------------------------------------*/


