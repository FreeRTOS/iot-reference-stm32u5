/*
 * FreeRTOS STM32 Reference Integration
 *
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 */

#ifndef MQTT_METRICS_H
#define MQTT_METRICS_H

#include "FreeRTOS.h"
#include "task.h"      /* For tskKERNEL_VERSION_NUMBER */
#include "core_mqtt.h" /* For MQTT_LIBRARY_VERSION */

/**
 * @brief The name of the operating system that the application is running on.
 * The current value is given as an example. Please update for your specific
 * operating system.
 */
#define METRICS_OS_NAME          "FreeRTOS"

/**
 * @brief The version of the operating system that the application is running
 * on. By default, this is the current FreeRTOS Kernel version.
 */
#define METRICS_OS_VERSION       tskKERNEL_VERSION_NUMBER

/**
 * @brief The name of the hardware platform the application is running on. The
 * current value is given as an example. Please update for your specific
 * hardware platform.
 */
#define METRICS_PLATFORM_NAME    "STM32U5xx IoT Discovery Kit"

/**
 * @brief The name of the MQTT library used and its version, following an "@"
 * symbol.
 */
#define METRICS_MQTT_LIB         "coreMQTT@" MQTT_LIBRARY_VERSION

/**
 * @brief ALPN (Application-Layer Protocol Negotiation) protocol name for AWS IoT MQTT.
 */
#define AWS_IOT_MQTT_ALPN        "x-amzn-mqtt-ca"

/**
 * @brief The MQTT metrics string expected by AWS IoT.
 */
#define AWS_IOT_METRICS_STRING                             \
    "?SDK=" METRICS_OS_NAME "&Version=" METRICS_OS_VERSION \
    "&Platform=" METRICS_PLATFORM_NAME "&MQTTLib=" METRICS_MQTT_LIB

/**
 * @brief The length of the MQTT metrics string expected by AWS IoT.
 */
#define AWS_IOT_METRICS_STRING_LENGTH    ( ( uint16_t ) ( sizeof( AWS_IOT_METRICS_STRING ) - 1 ) )

#endif /* MQTT_METRICS_H */
