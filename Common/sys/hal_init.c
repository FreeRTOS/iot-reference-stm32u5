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

#include "FreeRTOS.h"
#include "stm32u5xx_hal.h"
#include "hw_defs.h"
#include "task.h"
#include "b_u585i_iot02a_bus.h"
#include "b_u585i_iot02a_errno.h"

/* Global peripheral handles */
RTC_HandleTypeDef * pxHndlRtc = NULL;
SPI_HandleTypeDef * pxHndlSpi2 = NULL;
UART_HandleTypeDef * pxHndlUart1 = NULL;
DCACHE_HandleTypeDef * pxHndlDCache = NULL;
DMA_HandleTypeDef * pxHndlGpdmaCh4 = NULL;
DMA_HandleTypeDef * pxHndlGpdmaCh5 = NULL;
#ifndef TFM_PSA_API
RNG_HandleTypeDef * pxHndlRng = NULL;
#endif /* ! defined( TFM_PSA_API ) */
TIM_HandleTypeDef * pxHndlTim5 = NULL;
IWDG_HandleTypeDef * pxHwndIwdg = NULL;

/* local function prototypes */
static void SystemClock_Config( void );
static void hw_gpdma_init( void );
static void hw_cache_init( void );
static void hw_cache_deinit( void );
static void hw_rtc_init( void );
static void hw_gpio_init( void );
static void hw_spi2_msp_init( SPI_HandleTypeDef * pxHndlSpi );
static void hw_spi2_msp_deinit( SPI_HandleTypeDef * pxHndlSpi );
static void hw_spi_init( void );
static void hw_tim5_init( void );
static void hw_watchdog_init( void );

#ifndef TFM_PSA_API
static void hw_rng_init( void );
#endif /* ! defined( TFM_PSA_API ) */

void hw_init( void )
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /*
     * Initializes flash interface and systick timer.
     * Note: HAL_Init calls HAL_MspInit.
     */
    HAL_Init();
    HAL_PWREx_EnableVddIO2();

    /* PendSV_IRQn interrupt configuration */
    HAL_NVIC_SetPriority( PendSV_IRQn, 7, 0 );

    /* Configure the system clock */
    SystemClock_Config();

#ifndef TFM_PSA_API
    hw_cache_init();
#endif

    /* Initialize uart for logging before cli is up and running */
    vInitLoggingEarly();

    /* Initialize GPIO */
    hw_gpio_init();

    hw_gpdma_init();
/*    hw_rtc_init(); */
    hw_spi_init();

#ifndef TFM_PSA_API
    hw_rng_init();
#endif

    if( BSP_I2C2_Init() != BSP_ERROR_NONE )
    {
        LogError( "Failed to initialize BSP I2C interface." );
    }

    hw_tim5_init();

    hw_watchdog_init();
}

static void SystemClock_Config( void )
{
    HAL_StatusTypeDef xResult = HAL_OK;

    xResult = HAL_PWREx_ControlVoltageScaling( PWR_REGULATOR_VOLTAGE_SCALE1 );
    configASSERT( xResult == HAL_OK );

    RCC_OscInitTypeDef xRccOscInit =
    {
        .OscillatorType      = RCC_OSCILLATORTYPE_HSI48 | RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_MSI,
        .HSI48State          = RCC_HSI48_ON,
        .LSIState            = RCC_LSI_ON,
        .MSIState            = RCC_MSI_ON,
        .MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT,
        .MSIClockRange       = RCC_MSIRANGE_0,
        .LSIDiv              = RCC_LSI_DIV1,
        .PLL.PLLState        = RCC_PLL_ON,
        .PLL.PLLSource       = RCC_PLLSOURCE_MSI,
        .PLL.PLLMBOOST       = RCC_PLLMBOOST_DIV4,
        .PLL.PLLM            = 3,
        .PLL.PLLN            = 10,
        .PLL.PLLP            = 2,
        .PLL.PLLQ            = 2,
        .PLL.PLLR            = 1,
        .PLL.PLLRGE          = RCC_PLLVCIRANGE_1,
        .PLL.PLLFRACN        = 0,
    };

    /* Switching from one PLL configuration to another requires to temporarily restore the default RCC configuration. */
    xResult = HAL_RCC_DeInit();
    configASSERT( xResult == HAL_OK );

    xResult = HAL_RCC_OscConfig( &xRccOscInit );
    configASSERT( xResult == HAL_OK );

    const RCC_ClkInitTypeDef xRccClkInit =
    {
        .ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                          RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK3,
        .SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK,
        .AHBCLKDivider  = RCC_SYSCLK_DIV1,
        .APB1CLKDivider = RCC_HCLK_DIV1,
        .APB2CLKDivider = RCC_HCLK_DIV1,
        .APB3CLKDivider = RCC_HCLK_DIV1,
    };

    xResult = HAL_RCC_ClockConfig( &xRccClkInit, FLASH_LATENCY_4 );
    configASSERT( xResult == HAL_OK );
}

static void hw_gpdma_init( void )
{
    __HAL_RCC_GPDMA1_CLK_ENABLE();

    HAL_NVIC_SetPriority( GPDMA1_Channel4_IRQn, 5, 0 );
    HAL_NVIC_EnableIRQ( GPDMA1_Channel4_IRQn );

    HAL_NVIC_SetPriority( GPDMA1_Channel5_IRQn, 5, 0 );
    HAL_NVIC_EnableIRQ( GPDMA1_Channel5_IRQn );
}

static void hw_cache_init( void )
{
    HAL_StatusTypeDef xResult = HAL_OK;

    ( void ) HAL_ICACHE_Invalidate();
    ( void ) HAL_ICACHE_Disable();

    /* initialize ICACHE (makes flash access faster) */
    xResult = HAL_ICACHE_ConfigAssociativityMode( ICACHE_1WAY );

    configASSERT( xResult == HAL_OK );

    if( xResult == HAL_OK )
    {
        xResult = HAL_ICACHE_Invalidate();
        configASSERT( xResult == HAL_OK );
    }

    if( xResult == HAL_OK )
    {
        xResult = HAL_ICACHE_Enable();
        configASSERT( xResult == HAL_OK );
    }

    /* Initialize DCACHE */

    static DCACHE_HandleTypeDef xHndlDCache =
    {
        .Instance           = DCACHE1,
        .Init.ReadBurstType = DCACHE_READ_BURST_WRAP,
    };

    if( xResult == HAL_OK )
    {
        xResult = HAL_DCACHE_Init( &xHndlDCache );
        configASSERT( xResult == HAL_OK );
    }

    if( xResult == HAL_OK )
    {
        ( void ) HAL_DCACHE_Disable( &xHndlDCache );
        xResult = HAL_DCACHE_Invalidate( &xHndlDCache );
        configASSERT( xResult == HAL_OK );
    }

    if( xResult == HAL_OK )
    {
        xResult = HAL_DCACHE_Enable( &xHndlDCache );
        configASSERT( xResult == HAL_OK );
    }

    if( xResult == HAL_OK )
    {
        pxHndlDCache = &xHndlDCache;
    }
}

static void hw_cache_deinit( void )
{
    HAL_StatusTypeDef xResult = HAL_OK;

    if( pxHndlDCache != NULL )
    {
        xResult = HAL_DCACHE_Invalidate( pxHndlDCache );
        xResult &= HAL_DCACHE_Disable( pxHndlDCache );

        configASSERT( xResult == HAL_OK );
    }

    xResult = HAL_ICACHE_Invalidate();
    configASSERT( xResult == HAL_OK );

    xResult = HAL_ICACHE_Disable();
    configASSERT( xResult == HAL_OK );
}

static void hw_rtc_init( void )
{
    HAL_StatusTypeDef xResult = HAL_OK;

    static RTC_HandleTypeDef xHndlRtc =
    {
        .Instance            = RTC,
        .Init.HourFormat     = RTC_HOURFORMAT_24,
        .Init.AsynchPrediv   = 127,
        .Init.SynchPrediv    = 255,
        .Init.OutPut         = RTC_OUTPUT_DISABLE,
        .Init.OutPutRemap    = RTC_OUTPUT_REMAP_NONE,
        .Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH,
        .Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN,
        .Init.OutPutPullUp   = RTC_OUTPUT_PULLUP_NONE,
        .Init.BinMode        = RTC_BINARY_NONE,
    };

    xResult = HAL_RTC_Init( &xHndlRtc );
    configASSERT( xResult == HAL_OK );

    /* Export handle on success */
    if( xResult == HAL_OK )
    {
        pxHndlRtc = &xHndlRtc;
    }
}

static void hw_gpio_init( void )
{
    /* GPIO Ports Clock Enable */

    HAL_PWREx_EnableVddIO2();

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* LED Outputs */
    {
        GPIO_InitTypeDef xGpioInit =
        {
            .Pin       = LED_RED_Pin | LED_GREEN_Pin,
            .Mode      = GPIO_MODE_OUTPUT_PP,
            .Pull      = GPIO_NOPULL,
            .Speed     = GPIO_SPEED_FREQ_LOW,
            .Alternate = 0X0,
        };

        HAL_GPIO_Init( GPIOH, &xGpioInit );

        HAL_GPIO_WritePin( GPIOH, LED_RED_Pin | LED_GREEN_Pin, GPIO_PIN_RESET );
    }


    /* MXCHIP_FLOW_Pin Input */
    {
        GPIO_InitTypeDef xGpioInit =
        {
            .Pin       = MXCHIP_FLOW_Pin,
            .Mode      = GPIO_MODE_IT_RISING,
            .Pull      = GPIO_NOPULL,
            .Speed     = GPIO_SPEED_FREQ_VERY_HIGH,
            .Alternate = 0X0,
        };

        HAL_GPIO_Init( MXCHIP_FLOW_GPIO_Port, &xGpioInit );

        HAL_NVIC_SetPriority( MXCHIP_FLOW_EXTI_IRQn, 5, 3 );
        HAL_NVIC_EnableIRQ( MXCHIP_FLOW_EXTI_IRQn );
    }

    /* MXCHIP_NOTIFY_Pin Input */
    {
        GPIO_InitTypeDef xGpioInit =
        {
            .Pin       = MXCHIP_NOTIFY_Pin,
            .Mode      = GPIO_MODE_IT_RISING,
            .Pull      = GPIO_NOPULL,
            .Speed     = GPIO_SPEED_FREQ_VERY_HIGH,
            .Alternate = 0X0,
        };

        HAL_GPIO_Init( MXCHIP_NOTIFY_GPIO_Port, &xGpioInit );

        HAL_NVIC_SetPriority( MXCHIP_NOTIFY_EXTI_IRQn, 5, 4 );
        HAL_NVIC_EnableIRQ( MXCHIP_NOTIFY_EXTI_IRQn );
    }


    /* MXCHIP_NSS_Pin Output */
    {
        HAL_GPIO_WritePin( MXCHIP_NSS_GPIO_Port, MXCHIP_NSS_Pin, GPIO_PIN_SET );

        GPIO_InitTypeDef xGpioInit =
        {
            .Pin       = MXCHIP_NSS_Pin,
            .Mode      = GPIO_MODE_OUTPUT_PP,
            .Pull      = GPIO_NOPULL,
            .Speed     = GPIO_SPEED_FREQ_VERY_HIGH,
            .Alternate = 0X0,
        };

        HAL_GPIO_Init( MXCHIP_NSS_GPIO_Port, &xGpioInit );
    }

    /* MXCHIP_RESET_Pin Output */
    {
        HAL_GPIO_WritePin( MXCHIP_RESET_GPIO_Port, MXCHIP_RESET_Pin, GPIO_PIN_RESET );

        GPIO_InitTypeDef xGpioInit =
        {
            .Pin       = MXCHIP_RESET_Pin,
            .Mode      = GPIO_MODE_OUTPUT_PP,
            .Pull      = GPIO_NOPULL,
            .Speed     = GPIO_SPEED_FREQ_LOW,
            .Alternate = 0X0,
        };

        HAL_GPIO_Init( MXCHIP_RESET_GPIO_Port, &xGpioInit );
    }
}


/* Static MSP Callbacks ( for HAL modules that support callback registration ) */
static void hw_spi2_msp_init( SPI_HandleTypeDef * pxHndlSpi )
{
    HAL_StatusTypeDef xResult;

    configASSERT( pxHndlSpi != NULL );

    RCC_PeriphCLKInitTypeDef xRccPeriphClkInit =
    {
        .PeriphClockSelection = RCC_PERIPHCLK_SPI2,
        .Spi2ClockSelection   = RCC_SPI2CLKSOURCE_PCLK1,
    };

    xResult = HAL_RCCEx_PeriphCLKConfig( &xRccPeriphClkInit );
    configASSERT( xResult == HAL_OK );

    if( xResult == HAL_OK )
    {
        /* Peripheral clock enable */
        __HAL_RCC_SPI2_CLK_ENABLE();

        __HAL_RCC_GPIOD_CLK_ENABLE();
    }

    if( xResult == HAL_OK )
    {
        /*
         * SPI2 GPIO Configuration
         * PD4     ------> SPI2_MOSI
         * PD3     ------> SPI2_MISO
         * PD1     ------> SPI2_SCK
         */

        GPIO_InitTypeDef xGpioInit =
        {
            .Pin       = GPIO_PIN_4 | GPIO_PIN_3 | GPIO_PIN_1,
            .Mode      = GPIO_MODE_AF_PP,
            .Pull      = GPIO_NOPULL,
            .Speed     = GPIO_SPEED_FREQ_HIGH,
            .Alternate = GPIO_AF5_SPI2,
        };

        HAL_GPIO_Init( GPIOD, &xGpioInit );
    }

    static DMA_HandleTypeDef xHndlGpdmaCh4 =
    {
        .Instance                  = GPDMA1_Channel4,
        .Init                      =
        {
            .Request               = GPDMA1_REQUEST_SPI2_RX,
            .BlkHWRequest          = DMA_BREQ_SINGLE_BURST,
            .Direction             = DMA_PERIPH_TO_MEMORY,
            .SrcInc                = DMA_SINC_FIXED,
            .DestInc               = DMA_DINC_INCREMENTED,
            .SrcDataWidth          = DMA_SRC_DATAWIDTH_BYTE,
            .DestDataWidth         = DMA_DEST_DATAWIDTH_BYTE,
            .Priority              = DMA_LOW_PRIORITY_HIGH_WEIGHT,
            .SrcBurstLength        = 1,
            .DestBurstLength       = 1,
            .TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1,
            .TransferEventMode     = DMA_TCEM_BLOCK_TRANSFER,
            .Mode                  = DMA_NORMAL,
        },
    };

    if( xResult == HAL_OK )
    {
        xResult = HAL_DMA_Init( &xHndlGpdmaCh4 );
        configASSERT( xResult == HAL_OK );
    }

    if( xResult == HAL_OK )
    {
        __HAL_LINKDMA( pxHndlSpi, hdmarx, xHndlGpdmaCh4 );

        xResult = HAL_DMA_ConfigChannelAttributes( &xHndlGpdmaCh4, DMA_CHANNEL_NPRIV );

        configASSERT( xResult == HAL_OK );
    }

    static DMA_HandleTypeDef xHndlGpdmaCh5 =
    {
        .Instance                  = GPDMA1_Channel5,
        .Init                      =
        {
            .Request               = GPDMA1_REQUEST_SPI2_TX,
            .BlkHWRequest          = DMA_BREQ_SINGLE_BURST,
            .Direction             = DMA_MEMORY_TO_PERIPH,
            .SrcInc                = DMA_SINC_INCREMENTED,
            .DestInc               = DMA_DINC_FIXED,
            .SrcDataWidth          = DMA_SRC_DATAWIDTH_BYTE,
            .DestDataWidth         = DMA_DEST_DATAWIDTH_BYTE,
            .Priority              = DMA_LOW_PRIORITY_HIGH_WEIGHT,
            .SrcBurstLength        = 1,
            .DestBurstLength       = 1,
            .TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1,
            .TransferEventMode     = DMA_TCEM_BLOCK_TRANSFER,
            .Mode                  = DMA_NORMAL,
        },
    };

    xResult = HAL_DMA_Init( &xHndlGpdmaCh5 );
    configASSERT( xResult == HAL_OK );

    if( xResult == HAL_OK )
    {
        __HAL_LINKDMA( pxHndlSpi, hdmatx, xHndlGpdmaCh5 );

        xResult = HAL_DMA_ConfigChannelAttributes( &xHndlGpdmaCh5, DMA_CHANNEL_NPRIV );

        configASSERT( xResult == HAL_OK );
    }

    if( xResult == HAL_OK )
    {
        pxHndlGpdmaCh4 = &xHndlGpdmaCh4;
        pxHndlGpdmaCh5 = &xHndlGpdmaCh5;
        HAL_NVIC_SetPriority( SPI2_IRQn, 5, 0 );
        HAL_NVIC_EnableIRQ( SPI2_IRQn );
    }
}

static void hw_spi2_msp_deinit( SPI_HandleTypeDef * pxHndlSpi )
{
    configASSERT( pxHndlSpi != NULL );

    __HAL_RCC_SPI2_CLK_DISABLE();

    ( void ) HAL_GPIO_DeInit( GPIOD, GPIO_PIN_4 | GPIO_PIN_3 | GPIO_PIN_1 );

    ( void ) HAL_DMA_DeInit( pxHndlSpi->hdmatx );
    ( void ) HAL_DMA_DeInit( pxHndlSpi->hdmarx );

    HAL_NVIC_DisableIRQ( SPI2_IRQn );
}


static void hw_spi_init( void )
{
    HAL_StatusTypeDef xResult = HAL_OK;

    static SPI_HandleTypeDef xHndlSpi2 =
    {
        .Instance                        = SPI2,
        .Init.Mode                       = SPI_MODE_MASTER,
        .Init.Direction                  = SPI_DIRECTION_2LINES,
        .Init.DataSize                   = SPI_DATASIZE_8BIT,
        .Init.CLKPolarity                = SPI_POLARITY_LOW,
        .Init.CLKPhase                   = SPI_PHASE_1EDGE,
        .Init.NSS                        = SPI_NSS_SOFT,
        .Init.BaudRatePrescaler          = SPI_BAUDRATEPRESCALER_8,
        .Init.FirstBit                   = SPI_FIRSTBIT_MSB,
        .Init.TIMode                     = SPI_TIMODE_DISABLE,
        .Init.CRCCalculation             = SPI_CRCCALCULATION_DISABLE,
        .Init.CRCPolynomial              = 0x7,
        .Init.NSSPMode                   = SPI_NSS_PULSE_DISABLE,
        .Init.NSSPolarity                = SPI_NSS_POLARITY_LOW,
        .Init.FifoThreshold              = SPI_FIFO_THRESHOLD_01DATA,
        .Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN,
        .Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN,
        .Init.MasterSSIdleness           = SPI_MASTER_SS_IDLENESS_00CYCLE,
        .Init.MasterInterDataIdleness    = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE,
        .Init.MasterReceiverAutoSusp     = SPI_MASTER_RX_AUTOSUSP_DISABLE,
        .Init.MasterKeepIOState          = SPI_MASTER_KEEP_IO_STATE_DISABLE,
        .Init.IOSwap                     = SPI_IO_SWAP_DISABLE,
        .Init.ReadyMasterManagement      = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY,
        .Init.ReadyPolarity              = SPI_RDY_POLARITY_HIGH,
    };

    xResult = HAL_SPI_RegisterCallback( &xHndlSpi2, HAL_SPI_MSPINIT_CB_ID, &hw_spi2_msp_init );
    configASSERT( xResult == HAL_OK );

    xResult &= HAL_SPI_RegisterCallback( &xHndlSpi2, HAL_SPI_MSPDEINIT_CB_ID, &hw_spi2_msp_deinit );
    configASSERT( xResult == HAL_OK );

    if( xResult == HAL_OK )
    {
        xResult = HAL_SPI_Init( &xHndlSpi2 );

        configASSERT( xResult == HAL_OK );
    }

    SPI_AutonomousModeConfTypeDef xSpiAutoModeConf =
    {
        .TriggerState     = SPI_AUTO_MODE_DISABLE,
        .TriggerSelection = SPI_GRP1_GPDMA_CH0_TCF_TRG,
        .TriggerPolarity  = SPI_TRIG_POLARITY_RISING,
    };

    if( xResult == HAL_OK )
    {
        SPI2->CFG1 |= 0x00000007 << SPI_CFG1_CRCSIZE_Pos;

        xResult = HAL_SPIEx_SetConfigAutonomousMode( &xHndlSpi2, &xSpiAutoModeConf );
        configASSERT( xResult == HAL_OK );
    }

    /* Export handle on success */
    if( xResult == HAL_OK )
    {
        pxHndlSpi2 = &xHndlSpi2;
    }
}

#ifndef TFM_PSA_API
static void hw_rng_init( void )
{
    HAL_StatusTypeDef xResult = HAL_OK;
    RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };
    static RNG_HandleTypeDef xRngHandle =
    {
        .Instance                 = RNG,
        .Init.ClockErrorDetection = RNG_CED_ENABLE,
        .Lock                     = HAL_UNLOCKED,
        .State                    = HAL_RNG_STATE_RESET,
        .RandomNumber             = 0
    };
    uint32_t ulDummyValue = 0;

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RNG;
    PeriphClkInit.RngClockSelection = RCC_RNGCLKSOURCE_HSI48;

    xResult = HAL_RCCEx_PeriphCLKConfig( &PeriphClkInit );
    configASSERT( xResult == HAL_OK );

    /* Peripheral clock enable */
    __HAL_RCC_RNG_CLK_ENABLE();

    if( xResult == HAL_OK )
    {
        xResult = HAL_RNG_Init( &xRngHandle );
        configASSERT( xResult == HAL_OK );
    }

    /* Ignore first random value returned */
    ( void ) HAL_RNG_GenerateRandomNumber( &xRngHandle, &ulDummyValue );

    ( void ) ulDummyValue;

    if( xResult == HAL_OK )
    {
        pxHndlRng = &xRngHandle;
    }
}
#endif /* !defined( TFM_PSA_API ) */

static void hw_tim5_init( void )
{
    HAL_StatusTypeDef xResult = HAL_OK;

    static TIM_HandleTypeDef xTim5Handle =
    {
        .Instance       = TIM5,
        .Init.Prescaler = 4096, /* 160 MHz / 4096 = 39KHz */
        .Init.Period    = 0xFFFFFFFF,
    };

    __TIM5_CLK_ENABLE();

    xResult = HAL_TIM_Base_Init( &xTim5Handle );
    configASSERT( xResult == HAL_OK );

    if( xResult == HAL_OK )
    {
        xResult = HAL_TIM_Base_Start( &xTim5Handle );
        configASSERT( xResult == HAL_OK );
    }

    if( xResult == HAL_OK )
    {
        pxHndlTim5 = &xTim5Handle;
    }
}

static void hw_watchdog_init( void )
{
    HAL_StatusTypeDef xResult = HAL_OK;

    /* Default LSI clock = 32kHz, / 1024 => 31.25 hz => 32ms / tick
     */
    /* Set timeout to 10 seconds */

    static IWDG_HandleTypeDef xIwdgHandle =
    {
        .Instance       = IWDG,
        .Init.Prescaler = IWDG_PRESCALER_1024,
        .Init.Window    = IWDG_WINDOW_DISABLE,
        /* Reload value is 12-bit -> 0 - 4095 */
        .Init.Reload    = 315,
        .Init.EWI       = 0
    };

    xResult = HAL_IWDG_Init( &xIwdgHandle );

    configASSERT( xResult == HAL_OK );

    if( xResult == HAL_OK )
    {
        pxHwndIwdg = &xIwdgHandle;
    }
}

/* HAL MspInit Callbacks */
void HAL_MspInit( void )
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_DisableUCPDDeadBattery();
}

void HAL_I2C_MspDeInit( I2C_HandleTypeDef * pxHndlI2c )
{
    configASSERT( pxHndlI2c != NULL );

    if( pxHndlI2c->Instance == I2C2 )
    {
        __HAL_RCC_I2C2_CLK_DISABLE();
        HAL_GPIO_DeInit( GPIOH, GPIO_PIN_4 | GPIO_PIN_5 );
    }
}

void HAL_I2C_MspInit( I2C_HandleTypeDef * pxHndlI2c )
{
    HAL_StatusTypeDef xResult = HAL_OK;

    if( pxHndlI2c->Instance == I2C2 )
    {
        RCC_PeriphCLKInitTypeDef xRccPeriphClkInit =
        {
            .PeriphClockSelection = RCC_PERIPHCLK_I2C2,
            .Spi2ClockSelection   = RCC_I2C2CLKSOURCE_PCLK1,
        };

        xResult = HAL_RCCEx_PeriphCLKConfig( &xRccPeriphClkInit );
        configASSERT( xResult == HAL_OK );

        if( xResult == HAL_OK )
        {
            __HAL_RCC_GPIOH_CLK_ENABLE();

            GPIO_InitTypeDef xGpioInit =
            {
                .Pin       = GPIO_PIN_4 | GPIO_PIN_5,
                .Mode      = GPIO_MODE_AF_OD,
                .Pull      = GPIO_NOPULL,
                .Speed     = GPIO_SPEED_FREQ_HIGH,
                .Alternate = GPIO_AF4_I2C2,
            };

            HAL_GPIO_Init( GPIOH, &xGpioInit );

            /* Peripheral clock enable */
            __HAL_RCC_I2C2_CLK_ENABLE();
        }
    }
}

/* Override HAL Tick weak functions */
HAL_StatusTypeDef HAL_InitTick( uint32_t TickPriority )
{
    ( void ) TickPriority;
    ( void ) SysTick_Config( SystemCoreClock / 1000 );
    return HAL_OK;
}


void HAL_Delay( uint32_t ulDelayMs )
{
    if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
    {
        vTaskDelay( pdMS_TO_TICKS( ulDelayMs ) );
    }
    else
    {
        uint32_t ulStartTick = HAL_GetTick();
        uint32_t ulTicksWaited = ulDelayMs;

        /* Add a freq to guarantee minimum wait */
        if( ulTicksWaited < HAL_MAX_DELAY )
        {
            ulTicksWaited += ( uint32_t ) ( HAL_GetTickFreq() );
        }

        while( ( HAL_GetTick() - ulStartTick ) < ulTicksWaited )
        {
            __NOP();
        }
    }
}
