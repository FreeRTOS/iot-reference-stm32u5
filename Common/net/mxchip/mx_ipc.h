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
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 */

#ifndef _MXFREE_IPC_
#define _MXFREE_IPC_

#include "netif/ethernet.h"
#include "stdint.h"
#include "FreeRTOS.h"

typedef enum IPCError
{
    IPC_SUCCESS = 0,
    IPC_ERROR = -1,
    IPC_TIMEOUT = -2,
    IPC_IO_ERROR = -3,
    IPC_NO_MEMORY = -3,
    IPC_PARAMETER_ERROR = -4,
    IPC_ERROR_INTERNAL = -5
} IPCError_t;

typedef enum
{
    WIFI_BYPASS_MODE_STATION,
    WIFI_BYPASS_MODE_SOFTAP
} MxBypassMode_t;

typedef enum
{
    MX_STATUS_NONE = 0,
    MX_STATUS_STA_DOWN,
    MX_STATUS_STA_UP,
    MX_STATUS_STA_GOT_IP,
    MX_STATUS_AP_DOWN,
    MX_STATUS_AP_UP
} MxStatus_t;

typedef void ( * MxEventCallback_t )( MxStatus_t,
                                      void * );

IPCError_t mx_RequestVersion( char * pcVersionBuffer,
                              uint32_t ulVersionLength,
                              TickType_t xTimeout );

IPCError_t mx_FactoryReset( TickType_t xTimeout );

IPCError_t mx_GetMacAddress( struct eth_addr * pxMacAddress,
                             TickType_t xTimeout );

IPCError_t mx_Connect( const char * pcSSID,
                       const char * pcPSK,
                       TickType_t xTimeout );

IPCError_t mx_Disconnect( TickType_t xTimeout );

IPCError_t mx_SetBypassMode( BaseType_t xEnable,
                             TickType_t xTimeout );

IPCError_t mx_RegisterEventCallback( MxEventCallback_t pvCallback,
                                     void * pxCallbackContext );

#endif /* _MXFREE_IPC_ */
