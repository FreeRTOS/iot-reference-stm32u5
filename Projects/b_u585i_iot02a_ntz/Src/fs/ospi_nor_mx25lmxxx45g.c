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
#include "FreeRTOS.h"
#include "task.h"

#include "ospi_nor_mx25lmxxx45g.h"

static TaskHandle_t xTaskHandle = NULL;
static OSPI_HandleTypeDef * s_pxOSPI = NULL;

static inline void ospi_HandleCallback( OSPI_HandleTypeDef * pxOSPI,
                                        HAL_OSPI_CallbackIDTypeDef xCallbackId )
{
    configASSERT( pxOSPI != NULL );
    configASSERT( xTaskHandle != NULL );
    BaseType_t xHigherPriorityTaskWoken;

    xTaskNotifyIndexedFromISR( xTaskHandle, 1, xCallbackId, eSetValueWithOverwrite, &xHigherPriorityTaskWoken );
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

/* STM32 HAL Callbacks */
static void ospi_RxCpltCallback( OSPI_HandleTypeDef * pxOSPI )
{
    ospi_HandleCallback( pxOSPI, HAL_OSPI_RX_CPLT_CB_ID );
}

static void ospi_TxCpltCallback( OSPI_HandleTypeDef * pxOSPI )
{
    ospi_HandleCallback( pxOSPI, HAL_OSPI_TX_CPLT_CB_ID );
}

static void ospi_CmdCpltCallback( OSPI_HandleTypeDef * pxOSPI )
{
    ospi_HandleCallback( pxOSPI, HAL_OSPI_CMD_CPLT_CB_ID );
}

static void ospi_StatusMatchCpltCallback( OSPI_HandleTypeDef * pxOSPI )
{
    ospi_HandleCallback( pxOSPI, HAL_OSPI_STATUS_MATCH_CB_ID );
}

static void ospi_TimeoutCallback( OSPI_HandleTypeDef * pxOSPI )
{
    ospi_HandleCallback( pxOSPI, HAL_OSPI_TIMEOUT_CB_ID );
}

static void ospi_ErrorCallback( OSPI_HandleTypeDef * pxOSPI )
{
    ospi_HandleCallback( pxOSPI, HAL_OSPI_ERROR_CB_ID );
}

static void ospi_AbortCallback( OSPI_HandleTypeDef * pxOSPI )
{
    ospi_HandleCallback( pxOSPI, HAL_OSPI_ABORT_CB_ID );
}

static BaseType_t ospi_WaitForCallback( HAL_OSPI_CallbackIDTypeDef xCallbackID,
                                        TickType_t xTicksToWait )
{
    configASSERT( xCallbackID <= HAL_OSPI_TIMEOUT_CB_ID );
    configASSERT( xCallbackID >= HAL_OSPI_ERROR_CB_ID );

    TickType_t xRemainingTicks = xTicksToWait;
    TimeOut_t xTimeOut;

    uint32_t ulNotifyValue = 0xFFFFFFFF;

    vTaskSetTimeOutState( &xTimeOut );

    while( ulNotifyValue != xCallbackID )
    {
        ( void ) xTaskNotifyWaitIndexed( 1, 0x0, 0xFFFFFFFF, &ulNotifyValue, xRemainingTicks );

        if( xTaskCheckForTimeOut( &xTimeOut, &xRemainingTicks ) )
        {
            ulNotifyValue = 0xFFFFFFFF;
            break;
        }
    }

    return( ulNotifyValue == xCallbackID );
}

void OCTOSPI2_IRQHandler( void )
{
    configASSERT( s_pxOSPI != NULL );
    HAL_OSPI_IRQHandler( s_pxOSPI );
}

static void ospi_IRQHandler( void )
{
    configASSERT( s_pxOSPI != NULL );
    HAL_OSPI_IRQHandler( s_pxOSPI );
}

/* Initialize static variables for the current operation */
static inline void ospi_OpInit( OSPI_HandleTypeDef * pxOSPI )
{
    s_pxOSPI = pxOSPI;
    xTaskHandle = xTaskGetCurrentTaskHandle();
}

static void ospi_MspInitCallback( OSPI_HandleTypeDef * pxOSPI )
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };
    RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };
    HAL_StatusTypeDef xHalStatus = HAL_OK;

    ( void ) pxOSPI;

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_OSPI;
    PeriphClkInit.OspiClockSelection = RCC_OSPICLKSOURCE_SYSCLK;
    xHalStatus = HAL_RCCEx_PeriphCLKConfig( &PeriphClkInit );

    if( xHalStatus != HAL_OK )
    {
        LogError( "Error while configuring peripheral clock for OSPI2." );
    }

    /* Peripheral clock enable */
    __HAL_RCC_OSPI2_CLK_ENABLE();

    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /**OCTOSPI2 GPIO Configuration
     * PI5     ------> OCTOSPIM_P2_NCS
     * PH12     ------> OCTOSPIM_P2_IO7
     * PH10     ------> OCTOSPIM_P2_IO5
     * PH11     ------> OCTOSPIM_P2_IO6
     * PF0     ------> OCTOSPIM_P2_IO0
     * PH9     ------> OCTOSPIM_P2_IO4
     * PF1     ------> OCTOSPIM_P2_IO1
     * PF2     ------> OCTOSPIM_P2_IO2
     * PF3     ------> OCTOSPIM_P2_IO3
     * PF4     ------> OCTOSPIM_P2_CLK
     * PF12     ------> OCTOSPIM_P2_DQS
     */
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_OCTOSPI2;
    HAL_GPIO_Init( GPIOI, &GPIO_InitStruct );

    GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_OCTOSPI2;
    HAL_GPIO_Init( GPIOH, &GPIO_InitStruct );

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
                          | GPIO_PIN_4 | GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_OCTOSPI2;
    HAL_GPIO_Init( GPIOF, &GPIO_InitStruct );

    /* Set the vector, requires sram located vector table */
    NVIC_SetVector( OCTOSPI2_IRQn, ( uint32_t ) ospi_IRQHandler );

    /* OCTOSPI2 interrupt Init */
    HAL_NVIC_SetPriority( OCTOSPI2_IRQn, 5, 0 );
    HAL_NVIC_EnableIRQ( OCTOSPI2_IRQn );
}

static void ospi_MspDeInitCallback( OSPI_HandleTypeDef * pxOSPI )
{
    ( void ) pxOSPI;

    __HAL_RCC_OSPI2_CLK_DISABLE();

    /**OCTOSPI2 GPIO Configuration
     * PI5     ------> OCTOSPIM_P2_NCS
     * PH12     ------> OCTOSPIM_P2_IO7
     * PH10     ------> OCTOSPIM_P2_IO5
     * PH11     ------> OCTOSPIM_P2_IO6
     * PF0     ------> OCTOSPIM_P2_IO0
     * PH9     ------> OCTOSPIM_P2_IO4
     * PF1     ------> OCTOSPIM_P2_IO1
     * PF2     ------> OCTOSPIM_P2_IO2
     * PF3     ------> OCTOSPIM_P2_IO3
     * PF4     ------> OCTOSPIM_P2_CLK
     * PF12     ------> OCTOSPIM_P2_DQS
     */
    HAL_GPIO_DeInit( GPIOI, GPIO_PIN_5 );

    HAL_GPIO_DeInit( GPIOH, GPIO_PIN_12 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_9 );

    HAL_GPIO_DeInit( GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
                     | GPIO_PIN_4 | GPIO_PIN_12 );

    /* OCTOSPI2 interrupt DeInit */
    HAL_NVIC_DisableIRQ( OCTOSPI2_IRQn );
}

static BaseType_t ospi_InitDriver( OSPI_HandleTypeDef * pxOSPI )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;

    /* Initialize handle struct */
    pxOSPI->Instance = OCTOSPI2;
    pxOSPI->Init.FifoThreshold = 4;
    pxOSPI->Init.DualQuad = HAL_OSPI_DUALQUAD_DISABLE;
    pxOSPI->Init.MemoryType = HAL_OSPI_MEMTYPE_MACRONIX;
    pxOSPI->Init.DeviceSize = 26;
    pxOSPI->Init.ChipSelectHighTime = 2;
    pxOSPI->Init.FreeRunningClock = HAL_OSPI_FREERUNCLK_ENABLE;
    pxOSPI->Init.ClockMode = HAL_OSPI_CLOCK_MODE_0;
    pxOSPI->Init.WrapSize = HAL_OSPI_WRAP_NOT_SUPPORTED;
    pxOSPI->Init.ClockPrescaler = 4;
    pxOSPI->Init.SampleShifting = HAL_OSPI_SAMPLE_SHIFTING_NONE;
    pxOSPI->Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_ENABLE;
    pxOSPI->Init.ChipSelectBoundary = 0;
    pxOSPI->Init.DelayBlockBypass = HAL_OSPI_DELAY_BLOCK_USED;
    pxOSPI->Init.MaxTran = 0;
    pxOSPI->Init.Refresh = 0;

    /* Register MSP (pinmux) callbacks */
    xHalStatus = HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_MSP_INIT_CB_ID, ospi_MspInitCallback );
    xHalStatus &= HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_MSP_DEINIT_CB_ID, ospi_MspDeInitCallback );

    if( xHalStatus != HAL_OK )
    {
        LogError( "Error while setting OSPI MspInit and MspDeInit callbacks" );
        return pdFALSE;
    }

    xHalStatus = HAL_OSPI_Init( pxOSPI );

    if( xHalStatus != HAL_OK )
    {
        LogError( "Error while Initializing OSPI driver." );
        return pdFALSE;
    }

    /* Register additional callbacks */
    xHalStatus = HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_RX_CPLT_CB_ID, ospi_RxCpltCallback );
    xHalStatus &= HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_TX_CPLT_CB_ID, ospi_TxCpltCallback );
    xHalStatus &= HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_CMD_CPLT_CB_ID, ospi_CmdCpltCallback );
    xHalStatus &= HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_STATUS_MATCH_CB_ID, ospi_StatusMatchCpltCallback );
    xHalStatus &= HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_TIMEOUT_CB_ID, ospi_TimeoutCallback );
    xHalStatus &= HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_ERROR_CB_ID, ospi_ErrorCallback );
    xHalStatus &= HAL_OSPI_RegisterCallback( pxOSPI, HAL_OSPI_ABORT_CB_ID, ospi_AbortCallback );

    if( xHalStatus != HAL_OK )
    {
        LogError( "Error while register OSPI driver callbacks." );
        return pdFALSE;
    }

    OSPIM_CfgTypeDef xOspiMCfg = { 0 };

    xOspiMCfg.ClkPort = 2;
    xOspiMCfg.DQSPort = 2;
    xOspiMCfg.NCSPort = 2;
    xOspiMCfg.IOLowPort = HAL_OSPIM_IOPORT_2_LOW;
    xOspiMCfg.IOHighPort = HAL_OSPIM_IOPORT_2_HIGH;

    xHalStatus = HAL_OSPIM_Config( pxOSPI, &xOspiMCfg, HAL_OSPI_TIMEOUT_DEFAULT_VALUE );

    if( xHalStatus != HAL_OK )
    {
        LogError( "Error while Initializing OSPIM driver." );
        return pdFALSE;
    }

    HAL_OSPI_DLYB_CfgTypeDef xOspiDlybCfg = { 0 };

    xOspiDlybCfg.Units = 56;
    xOspiDlybCfg.PhaseSel = 2;
    xHalStatus = HAL_OSPI_DLYB_SetConfig( pxOSPI, &xOspiDlybCfg );

    if( xHalStatus != HAL_OK )
    {
        LogError( "Error while Initializing OSPI DLYB driver." );
        return pdFALSE;
    }

    __HAL_OSPI_DISABLE( pxOSPI );
    vTaskDelay( 100 );

    SET_BIT( pxOSPI->Instance->DCR1, OCTOSPI_DCR1_FRCK );
    vTaskDelay( 100 );

    __HAL_OSPI_ENABLE( pxOSPI );
    vTaskDelay( 100 );

    DLYB_OCTOSPI2_NS->CR = 0U;
    DLYB_OCTOSPI2_NS->CR = 0x03;
    DLYB_OCTOSPI2_NS->CFGR = 0x7A02;
    DLYB_OCTOSPI2_NS->CR = 0x01;
    vTaskDelay( 100 );

    __HAL_OSPI_DISABLE( pxOSPI );
    vTaskDelay( 100 );

    CLEAR_BIT( pxOSPI->Instance->DCR1, OCTOSPI_DCR1_FRCK );
    vTaskDelay( 100 );

    __HAL_OSPI_ENABLE( pxOSPI );
    vTaskDelay( 100 );

    return pdTRUE;
}

static void ospi_AbortTransaction( OSPI_HandleTypeDef * pxOSPI,
                                   TickType_t xTimeout )
{
    ( void ) HAL_OSPI_Abort_IT( pxOSPI );
    ( void ) ospi_WaitForCallback( HAL_OSPI_ABORT_CB_ID, xTimeout );
}

static BaseType_t ospi_cmd_OPI_WREN( OSPI_HandleTypeDef * pxOSPI,
                                     TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;
    BaseType_t xSuccess = pdTRUE;

    OSPI_RegularCmdTypeDef xCmd =
    {
        .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
        .FlashId            = HAL_OSPI_FLASH_ID_1,

        .Instruction        = MX25LM_OPI_WREN,
        .InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES, /* 8 line STR mode */
        .InstructionSize    = HAL_OSPI_INSTRUCTION_16_BITS, /* 2 byte instructions */
        .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,

        .AddressMode        = HAL_OSPI_ADDRESS_NONE,

        .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,

        .DataMode           = HAL_OSPI_DATA_NONE,

        .DummyCycles        = 0,
        .DQSMode            = HAL_OSPI_DQS_DISABLE,
        .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
    };

    /* Clear notification state */
    ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

    /* Send command */
    xHalStatus = HAL_OSPI_Command_IT( pxOSPI, &xCmd );

    /* Wait for command complete callback */
    if( xHalStatus == HAL_OK )
    {
        xSuccess = ospi_WaitForCallback( HAL_OSPI_CMD_CPLT_CB_ID, xTimeout );
    }
    else
    {
        xSuccess = pdFALSE;
    }

    return( xSuccess );
}

static BaseType_t ospi_OPI_WaitForStatus( OSPI_HandleTypeDef * pxOSPI,
                                          uint32_t ulMask,
                                          uint32_t ulMatch,
                                          TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;
    BaseType_t xSuccess = pdTRUE;

    /* Setup a read of the status register */
    OSPI_RegularCmdTypeDef xCmd =
    {
        .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
        .FlashId            = HAL_OSPI_FLASH_ID_1,

        .Instruction        = MX25LM_OPI_RDSR,
        .InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES, /* 8 line STR mode */
        .InstructionSize    = HAL_OSPI_INSTRUCTION_16_BITS, /* 2 byte instructions */
        .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,

        .Address            = 0x00000000,                   /* Address = 0 for RDSR */
        .AddressMode        = HAL_OSPI_ADDRESS_8_LINES,
        .AddressSize        = HAL_OSPI_ADDRESS_32_BITS,
        .AddressDtrMode     = HAL_OSPI_DATA_DTR_DISABLE,

        .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,

        .DataMode           = HAL_OSPI_DATA_8_LINES,
        .DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE,
        .NbData             = 1,                            /* RDSR reg is 1 byte of data */

        .DummyCycles        = 4,                            /* PM2357 R1.1 pg 23, Note 5 => 4 dummy cycles */
        .DQSMode            = HAL_OSPI_DQS_DISABLE,
        .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
    };

    /* Send command */
    xHalStatus = HAL_OSPI_Command( pxOSPI, &xCmd, xTimeout );

    OSPI_AutoPollingTypeDef xPollingCfg =
    {
        .MatchMode     = HAL_OSPI_MATCH_MODE_AND,
        .AutomaticStop = HAL_OSPI_AUTOMATIC_STOP_ENABLE,
        .Interval      = 0x10,
        .Match         = ulMatch,
        .Mask          = ulMask,
    };

    /* Clear notification state */
    ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

    if( xHalStatus == HAL_OK )
    {
        /* Start auto-polling */
        xHalStatus = HAL_OSPI_AutoPolling_IT( pxOSPI, &xPollingCfg );

        if( xHalStatus != HAL_OK )
        {
            xSuccess = pdFALSE;
        }
    }

    if( xSuccess == pdTRUE )
    {
        xSuccess = ospi_WaitForCallback( HAL_OSPI_STATUS_MATCH_CB_ID, xTimeout );
    }

    /* Abort the ongoing transaction upon failure */
    if( xSuccess == pdFALSE )
    {
        ( void ) ospi_AbortTransaction( pxOSPI, xTimeout );
    }

    return xSuccess;
}


static BaseType_t ospi_SPI_WaitForStatus( OSPI_HandleTypeDef * pxOSPI,
                                          uint32_t ulMask,
                                          uint32_t ulMatch,
                                          TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;
    BaseType_t xSuccess = pdTRUE;

    /* Setup a read of the status register */
    OSPI_RegularCmdTypeDef xCmd =
    {
        .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
        .FlashId            = HAL_OSPI_FLASH_ID_1,

        .Instruction        = MX25LM_SPI_RDSR,
        .InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE,
        .InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS,
        .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,

        .AddressMode        = HAL_OSPI_ADDRESS_NONE,
        .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,

        .DataMode           = HAL_OSPI_DATA_1_LINE,
        .DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE,
        .NbData             = 1,

        .DummyCycles        = 0,
        .DQSMode            = HAL_OSPI_DQS_DISABLE,
        .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
    };

    /* Send command */
    xHalStatus = HAL_OSPI_Command( pxOSPI, &xCmd, xTimeout );

    OSPI_AutoPollingTypeDef xPollingCfg =
    {
        .MatchMode     = HAL_OSPI_MATCH_MODE_AND,
        .AutomaticStop = HAL_OSPI_AUTOMATIC_STOP_ENABLE,
        .Interval      = 0x10,
        .Match         = ulMatch,
        .Mask          = ulMask,
    };

    /* Clear notification state */
    ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

    if( xHalStatus == HAL_OK )
    {
        /* Start auto-polling */
        xHalStatus = HAL_OSPI_AutoPolling_IT( pxOSPI, &xPollingCfg );

        if( xHalStatus != HAL_OK )
        {
            xSuccess = pdFALSE;
        }
    }

    if( xSuccess == pdTRUE )
    {
        xSuccess = ospi_WaitForCallback( HAL_OSPI_STATUS_MATCH_CB_ID, xTimeout );
    }

    /* Abort the ongoing transaction upon failure */
    if( xSuccess == pdFALSE )
    {
        ( void ) ospi_AbortTransaction( pxOSPI, xTimeout );
    }

    return xSuccess;
}


/* send Write enable command (WREN) in SPI mode */
static BaseType_t ospi_cmd_SPI_WREN( OSPI_HandleTypeDef * pxOSPI,
                                     TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;
    OSPI_RegularCmdTypeDef xCmd =
    {
        .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
        .FlashId            = HAL_OSPI_FLASH_ID_1,
        .Instruction        = MX25LM_SPI_WREN,
        .InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE,
        .InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS,
        .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,
        .Address            = 0,
        .AddressMode        = HAL_OSPI_ADDRESS_NONE,
        .AddressSize        = 0,
        .AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE,
        .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,
        .DataMode           = HAL_OSPI_DATA_NONE,
        .NbData             = 0,
        .DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE,
        .DummyCycles        = 0,
        .DQSMode            = HAL_OSPI_DQS_DISABLE,
        .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
    };

    ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

    xHalStatus = HAL_OSPI_Command_IT( pxOSPI, &xCmd );

    if( xHalStatus == HAL_OK )
    {
        if( ospi_WaitForCallback( HAL_OSPI_CMD_CPLT_CB_ID, xTimeout ) != pdTRUE )
        {
            xHalStatus = -1;
        }
    }

    return( xHalStatus == HAL_OK );
}


/*
 * Switch flash from 1 bit SPI mode to 8 bit STR mode (single bit per clock)
 */
static BaseType_t ospi_cmd_SPI_8BitSTRMode( OSPI_HandleTypeDef * pxOSPI,
                                            TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;

    OSPI_RegularCmdTypeDef xCmd =
    {
        .OperationType         = HAL_OSPI_OPTYPE_COMMON_CFG,
        .FlashId               = HAL_OSPI_FLASH_ID_1,

        .Instruction           = MX25LM_SPI_WRCR2,
        .InstructionMode       = HAL_OSPI_INSTRUCTION_1_LINE,
        .InstructionSize       = HAL_OSPI_INSTRUCTION_8_BITS,
        .InstructionDtrMode    = HAL_OSPI_INSTRUCTION_DTR_DISABLE,

        .Address               = 0x0,
        .AddressMode           = HAL_OSPI_ADDRESS_1_LINE,
        .AddressSize           = HAL_OSPI_ADDRESS_32_BITS,
        .AddressDtrMode        = HAL_OSPI_ADDRESS_DTR_DISABLE,

        .AlternateBytesMode    = HAL_OSPI_ALTERNATE_BYTES_1_LINE,
        .AlternateBytesSize    = HAL_OSPI_ALTERNATE_BYTES_8_BITS,
        .AlternateBytes        = MX25LM_REG_CR2_0_SOPI,
        .AlternateBytesDtrMode = HAL_OSPI_ALTERNATE_BYTES_DTR_DISABLE,

        .DataMode              = HAL_OSPI_DATA_NONE,
        .NbData                = 0,
        .DataDtrMode           = HAL_OSPI_DATA_DTR_DISABLE,

        .DummyCycles           = 0,
        .DQSMode               = HAL_OSPI_DQS_DISABLE,
        .SIOOMode              = HAL_OSPI_SIOO_INST_EVERY_CMD
    };

    ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

    xHalStatus = HAL_OSPI_Command_IT( pxOSPI, &xCmd );

    if( ( xHalStatus == HAL_OK ) &&
        ( ospi_WaitForCallback( HAL_OSPI_CMD_CPLT_CB_ID, xTimeout ) != pdTRUE ) )
    {
        xHalStatus = -1;
    }

    return( xHalStatus == HAL_OK );
}



/*
 * @Brief Initialize octospi flash controller and related peripherals
 */
BaseType_t ospi_Init( OSPI_HandleTypeDef * pxOSPI )
{
    BaseType_t xSuccess = pdTRUE;

    ospi_OpInit( pxOSPI );

    xSuccess = ospi_InitDriver( pxOSPI );

    if( xSuccess != pdTRUE )
    {
        LogError( "Failed to initialize ospi driver." );
    }
    else
    {
        /* Set Write enable bit */
        xSuccess = ospi_cmd_SPI_WREN( pxOSPI, MX25LM_DEFAULT_TIMEOUT_MS );
    }

    if( xSuccess != pdTRUE )
    {
        LogError( "Failed to send WREN command." );
    }
    else
    {
        xSuccess = ospi_SPI_WaitForStatus( pxOSPI,
                                           MX25LM_REG_SR_WIP | MX25LM_REG_SR_WEL,
                                           MX25LM_REG_SR_WEL,
                                           MX25LM_DEFAULT_TIMEOUT_MS );
    }

    if( xSuccess != pdTRUE )
    {
        LogError( "Timed out while waiting for write enable." );
    }
    else
    {
        /* Enter 8 bit data mode */
        xSuccess = ospi_cmd_SPI_8BitSTRMode( pxOSPI, MX25LM_DEFAULT_TIMEOUT_MS );
    }

    if( xSuccess != pdTRUE )
    {
        LogError( "Failed to set data mode to 8Bit STR." );
    }
    else
    {
        /* Wait for WEL and WIP bits to clear */
        xSuccess = ospi_OPI_WaitForStatus( pxOSPI,
                                           MX25LM_REG_SR_WIP | MX25LM_REG_SR_WEL,
                                           0x0,
                                           MX25LM_DEFAULT_TIMEOUT_MS );
    }

    return xSuccess;
}

BaseType_t ospi_ReadAddr( OSPI_HandleTypeDef * pxOSPI,
                          uint32_t ulAddr,
                          void * pxBuffer,
                          uint32_t ulBufferLen,
                          TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;
    BaseType_t xSuccess = pdTRUE;

    ospi_OpInit( pxOSPI );

    if( pxOSPI == NULL )
    {
        xSuccess = pdFALSE;
        LogError( "pxOSPI is NULL." );
    }

    if( ulAddr >= MX25LM_MEM_SZ_BYTES )
    {
        xSuccess = pdFALSE;
        LogError( "Address is out of range." );
    }

    if( pxBuffer == NULL )
    {
        xSuccess = pdFALSE;
        LogError( "pxBuffer is NULL." );
    }

    if( ulBufferLen == 0 )
    {
        xSuccess = pdFALSE;
        LogError( "ulBufferLen is 0." );
    }

    /*TODO is there a limit to the number of bytes read? */

    /* Wait for idle condition (WIP bit should be 0) */
    xSuccess = ospi_OPI_WaitForStatus( pxOSPI,
                                       MX25LM_REG_SR_WIP,
                                       0x0,
                                       MX25LM_DEFAULT_TIMEOUT_MS );

    if( xSuccess != pdTRUE )
    {
        ospi_AbortTransaction( pxOSPI, MX25LM_DEFAULT_TIMEOUT_MS );
        LogError( "Timed out while waiting for OSPI IDLE condition." );
    }
    else
    {
        /* Setup an 8READ transaction */
        OSPI_RegularCmdTypeDef xCmd =
        {
            .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
            .FlashId            = HAL_OSPI_FLASH_ID_1,

            .Instruction        = MX25LM_OPI_8READ,
            .InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES, /* 8 line STR mode */
            .InstructionSize    = HAL_OSPI_INSTRUCTION_16_BITS, /* 2 byte instructions */
            .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,

            .Address            = ulAddr,
            .AddressMode        = HAL_OSPI_ADDRESS_8_LINES,
            .AddressSize        = HAL_OSPI_ADDRESS_32_BITS,
            .AddressDtrMode     = HAL_OSPI_DATA_DTR_DISABLE,

            .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,

            .DataMode           = HAL_OSPI_DATA_8_LINES,
            .DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE,
            .NbData             = ulBufferLen,

            .DummyCycles        = MX25LM_8READ_DUMMY_CYCLES,
            .DQSMode            = HAL_OSPI_DQS_DISABLE,
            .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
        };

        /* Clear notification state */
        ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

        /* Send command */
        xHalStatus = HAL_OSPI_Command( pxOSPI, &xCmd, MX25LM_DEFAULT_TIMEOUT_MS );

        if( xHalStatus != HAL_OK )
        {
            xSuccess = pdFALSE;
            ospi_AbortTransaction( pxOSPI, MX25LM_DEFAULT_TIMEOUT_MS );
            LogError( "Failed to send 8READ command." );
        }
    }

    if( xSuccess == pdTRUE )
    {
        /* Clear notification state */
        ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

        xHalStatus = HAL_OSPI_Receive_IT( pxOSPI, pxBuffer );

        /* Wait for receive op to complete */
        if( xHalStatus == HAL_OK )
        {
            xSuccess = ospi_WaitForCallback( HAL_OSPI_RX_CPLT_CB_ID, xTimeout );
        }
    }

    return( xSuccess );
}

/*
 * @Brief write up to 256 bytes to the given address.
 */
BaseType_t ospi_WriteAddr( OSPI_HandleTypeDef * pxOSPI,
                           uint32_t ulAddr,
                           const void * pxBuffer,
                           uint32_t ulBufferLen,
                           TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;
    BaseType_t xSuccess = pdTRUE;

    ospi_OpInit( pxOSPI );

    if( pxOSPI == NULL )
    {
        xSuccess = pdFALSE;
    }

    if( ( ulBufferLen > 256 ) ||
        ( ulBufferLen == 0 ) )
    {
        xSuccess = pdFALSE;
    }

    if( pxBuffer == NULL )
    {
        xSuccess = pdFALSE;
    }

    if( xSuccess == pdTRUE )
    {
        /* Wait for idle condition (WIP bit should be 0) */
        xSuccess = ospi_OPI_WaitForStatus( pxOSPI,
                                           MX25LM_REG_SR_WIP,
                                           0x0,
                                           xTimeout );
    }

    if( xSuccess == pdTRUE )
    {
        /* Enable write */
        xSuccess = ospi_cmd_OPI_WREN( pxOSPI, xTimeout );
    }

    /* Wait for Write Enable Latch */
    if( xSuccess == pdTRUE )
    {
        xSuccess = ospi_OPI_WaitForStatus( pxOSPI,
                                           MX25LM_REG_SR_WEL | MX25LM_REG_SR_WIP,
                                           MX25LM_REG_SR_WEL,
                                           xTimeout );
    }

    if( xSuccess == pdTRUE )
    {
        /* Setup a Program operation */
        OSPI_RegularCmdTypeDef xCmd =
        {
            .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
            .FlashId            = HAL_OSPI_FLASH_ID_1,

            .Instruction        = MX25LM_OPI_PP,
            .InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES, /* 8 line STR mode */
            .InstructionSize    = HAL_OSPI_INSTRUCTION_16_BITS, /* 2 byte instructions */
            .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,

            .Address            = ulAddr,
            .AddressMode        = HAL_OSPI_ADDRESS_8_LINES,
            .AddressSize        = HAL_OSPI_ADDRESS_32_BITS,
            .AddressDtrMode     = HAL_OSPI_DATA_DTR_DISABLE,

            .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,

            .DataMode           = HAL_OSPI_DATA_8_LINES,
            .DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE,
            .NbData             = ulBufferLen,

            .DummyCycles        = 0,
            .DQSMode            = HAL_OSPI_DQS_DISABLE,
            .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
        };

        /* Send command */
        xHalStatus = HAL_OSPI_Command( pxOSPI, &xCmd, xTimeout );
    }

    /* Clear notification state */
    ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

    if( xHalStatus != HAL_OK )
    {
        xSuccess = pdFALSE;
    }
    else
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
        xHalStatus = HAL_OSPI_Transmit_IT( pxOSPI, pxBuffer );
#pragma GCC diagnostic pop
    }

    if( xHalStatus != HAL_OK )
    {
        xSuccess = pdFALSE;
    }
    else
    {
        xSuccess = ospi_WaitForCallback( HAL_OSPI_TX_CPLT_CB_ID, xTimeout );
    }

    if( xSuccess == pdTRUE )
    {
        vTaskDelay( 1 );

        /* Wait for idle condition (WIP bit should be 0) */
        xSuccess = ospi_OPI_WaitForStatus( pxOSPI,
                                           MX25LM_REG_SR_WIP | MX25LM_REG_SR_WEL,
                                           0x0,
                                           xTimeout );
    }

    return xSuccess;
}

BaseType_t ospi_EraseSector( OSPI_HandleTypeDef * pxOSPI,
                             uint32_t ulAddr,
                             TickType_t xTimeout )
{
    HAL_StatusTypeDef xHalStatus = HAL_OK;
    BaseType_t xSuccess = pdTRUE;

    ospi_OpInit( pxOSPI );

    if( pxOSPI == NULL )
    {
        xSuccess = pdFALSE;
    }

    /* Validate Address */
    if( ulAddr >= MX25LM_MEM_SZ_BYTES )
    {
        xSuccess = pdFALSE;
    }

    if( xSuccess == pdTRUE )
    {
        /* Wait for idle condition (WIP bit should be 0) */
        xSuccess = ospi_OPI_WaitForStatus( pxOSPI,
                                           MX25LM_REG_SR_WIP,
                                           0x0,
                                           xTimeout );
    }

    if( xSuccess == pdTRUE )
    {
        /* Enable write */
        xSuccess = ospi_cmd_OPI_WREN( pxOSPI, xTimeout );
    }

    /* Wait for Write Enable Latch */
    if( xSuccess == pdTRUE )
    {
        xSuccess = ospi_OPI_WaitForStatus( pxOSPI,
                                           MX25LM_REG_SR_WEL,
                                           MX25LM_REG_SR_WEL,
                                           xTimeout );
    }

    if( xSuccess == pdTRUE )
    {
        /* Setup a Sector Erase operation */
        OSPI_RegularCmdTypeDef xCmd =
        {
            .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
            .FlashId            = HAL_OSPI_FLASH_ID_1,

            .Instruction        = MX25LM_OPI_SE,
            .InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES, /* 8 line STR mode */
            .InstructionSize    = HAL_OSPI_INSTRUCTION_16_BITS, /* 2 byte instructions */
            .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,

            .Address            = ulAddr,
            .AddressMode        = HAL_OSPI_ADDRESS_8_LINES,
            .AddressSize        = HAL_OSPI_ADDRESS_32_BITS,
            .AddressDtrMode     = HAL_OSPI_DATA_DTR_DISABLE,

            .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,

            .DataMode           = HAL_OSPI_DATA_NONE,
            .DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE,
            .NbData             = 0,

            .DummyCycles        = 0,
            .DQSMode            = HAL_OSPI_DQS_DISABLE,
            .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
        };

        /* Clear notification state */
        ( void ) xTaskNotifyStateClearIndexed( NULL, 1 );

        /* Send command */
        xHalStatus = HAL_OSPI_Command_IT( pxOSPI, &xCmd );
    }

    if( xHalStatus != HAL_OK )
    {
        xSuccess = pdFALSE;
    }
    else
    {
        xSuccess = ospi_WaitForCallback( HAL_OSPI_CMD_CPLT_CB_ID, xTimeout );
    }

    if( xSuccess == pdTRUE )
    {
        vTaskDelay( 1 );
        /* Wait for idle condition (WIP bit should be 0) */
        xSuccess = ospi_OPI_WaitForStatus( pxOSPI,
                                           MX25LM_REG_SR_WEL | MX25LM_REG_SR_WIP,
                                           0x0,
                                           xTimeout );
    }

    return( xSuccess );
}
