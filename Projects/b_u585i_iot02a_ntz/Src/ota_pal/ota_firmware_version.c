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

/**
 * @file ota_firmware_version.c Contains the Non-Trustzone firmware version number for
 * OTA update.
 */

#include "ota_appversion32.h"
#include "ota_config.h"
#include "test_param_config.h"
#include "test_execution_config.h"


/**
 *  @brief The version for the firmware which is running. OTA agent uses this
 * version number to perform anti-rollback validation. The firmware version for the
 * download image should be higher than the current version, otherwise the new image is
 * rejected in self test phase.1
 */
#if ( TEST_AUTOMATION_INTEGRATION == 1 ) && ( OTA_E2E_TEST_ENABLED == 1 )
#define APP_VERSION_MAJOR    OTA_APP_VERSION_MAJOR
#define APP_VERSION_MINOR    OTA_APP_VERSION_MINOR
#define APP_VERSION_BUILD    OTA_APP_VERSION_BUILD
#else

#define APP_VERSION_MAJOR    0
#define APP_VERSION_MINOR    9
#define APP_VERSION_BUILD    1
#endif /* ( TEST_AUTOMATION_INTEGRATION == 1 ) && ( OTA_E2E_TEST_ENABLED == 1 ) */



/**
 * @brief Structure used for encoding firmware version.
 */
const AppVersion32_t appFirmwareVersion =
{
    .u.x.major = APP_VERSION_MAJOR,
    .u.x.minor = APP_VERSION_MINOR,
    .u.x.build = APP_VERSION_BUILD,
};
