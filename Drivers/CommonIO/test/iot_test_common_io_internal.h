/*
 * FreeRTOS Common IO V0.1.3
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
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/*******************************************************************************
 * IOT On-Target Unit Test
 * @File: iot_test_common_io_internal.h
 * @Brief: File for IOT HAL test board specific configuration setup
 ******************************************************************************/

#include "iot_test_common_io_config.h"

#pragma once


/*
 * Test iteration count macros that platforms should set
 * in their repective "test_iot_config.h" file. If not set
 * there, the default value of 1 is set here.
 */

#if defined( IOT_TEST_COMMON_IO_UART_SUPPORTED ) && ( IOT_TEST_COMMON_IO_UART_SUPPORTED >= 1 )

/*
 * The variables below are used in the tests. Generally. they should be
 * set in SET_TEST_IOT_RESET_CONFIG() defined in the respective platform
 * directory. If not set, the default value set at the variable
 * definition is used.
 */

/* Uart */
    extern uint8_t uctestIotUartPort; /* The index of the UART that will be tested */

/**
 * Board specific UART config set
 *
 * @param: testSet: number of config set to be test
 * @return None
 */
    void SET_TEST_IOT_UART_CONFIG( int testSet );
#else
    #define IOT_TEST_COMMON_IO_UART_SUPPORTED    0
#endif


/* SPI */
#if defined( IOT_TEST_COMMON_IO_SPI_SUPPORTED ) && ( IOT_TEST_COMMON_IO_SPI_SUPPORTED >= 1 )
    extern uint32_t ultestIotSpiInstance;         /* SPI instance that we plan to test */
    extern uint32_t ulAssistedTestIotSpiInstance; /* SPI assisted tests */
    extern uint32_t ultestIotSpiSlave;            /* SPI assisted tests */
    extern uint32_t ulAssistedTestIotSpiSlave;    /* SPI assisted tests */
    extern IotSPIMode_t xtestIotSPIDefaultConfigMode;
    extern IotSPIBitOrder_t xtestIotSPIDefaultconfigBitOrder;
    extern uint32_t ultestIotSPIFrequency;
    extern uint32_t ultestIotSPIDummyValue;

/**
 * Board specific SPI config set
 *
 * @param: testSet: number of config set to be test
 * @return None
 */
    void SET_TEST_IOT_SPI_CONFIG( int testSet );
#else /* if defined( IOT_TEST_COMMON_IO_SPI_SUPPORTED ) && ( IOT_TEST_COMMON_IO_SPI_SUPPORTED >= 1 ) */
    #define IOT_TEST_COMMON_IO_SPI_SUPPORTED    0
#endif /* if defined( IOT_TEST_COMMON_IO_SPI_SUPPORTED ) && ( IOT_TEST_COMMON_IO_SPI_SUPPORTED >= 1 ) */
