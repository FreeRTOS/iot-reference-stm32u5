/*
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

#ifndef _KVSTORE_CONFIG_H
#define _KVSTORE_CONFIG_H

#include "kvstore_config_plat.h"

typedef enum KvStoreEnum
{
    CS_CORE_THING_NAME,
    CS_CORE_MQTT_ENDPOINT,
    CS_CORE_MQTT_PORT,
    CS_WIFI_SSID,
    CS_WIFI_CREDENTIAL,
    CS_TIME_HWM_S_1970,
    CS_NUM_KEYS
} KVStoreKey_t;

/* Define default values for common attributes */
#define THING_NAME_DFLT       ""
#define MQTT_ENDPOINT_DFLT    ""
#define MQTT_PORT_DFLT        8883

#define WIFI_SSID_DFLT        ""
#define WIFI_PASSWORD_DFLT    ""
#define WIFI_SECURITY_DFLT

/* Array to map between strings and KVStoreKey_t IDs */
#define KV_STORE_STRINGS   \
    {                      \
        "thing_name",      \
        "mqtt_endpoint",   \
        "mqtt_port",       \
        "wifi_ssid",       \
        "wifi_credential", \
        "time_hwm"         \
    }

#define KV_STORE_DEFAULTS                                                          \
    {                                                                              \
        KV_DFLT( KV_TYPE_STRING, THING_NAME_DFLT ),    /* CS_CORE_THING_NAME */    \
        KV_DFLT( KV_TYPE_STRING, MQTT_ENDPOINT_DFLT ), /* CS_CORE_MQTT_ENDPOINT */ \
        KV_DFLT( KV_TYPE_UINT32, MQTT_PORT_DFLT ),     /* CS_CORE_MQTT_PORT */     \
        KV_DFLT( KV_TYPE_STRING, WIFI_SSID_DFLT ),     /* CS_WIFI_SSID */          \
        KV_DFLT( KV_TYPE_STRING, WIFI_PASSWORD_DFLT ), /* CS_WIFI_CREDENTIAL */    \
        KV_DFLT( KV_TYPE_UINT32, 0 ),                  /* CS_TIME_HWM_S_1970 */    \
    }

#endif /* _KVSTORE_CONFIG_H */
