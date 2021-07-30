/**
  ******************************************************************************
  * @file    net_conf.h
  * @author  MCD Application Team
  * @brief   Configures the network socket APIs.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef NET_CONF_H
#define NET_CONF_H

/* Includes ------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* disable Misra rule to enable doxygen comment , A section of code appear to have been commented out */

#include <stdio.h>

#include "logging_levels.h"

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_ERROR
#endif /* LOG_LEVEL */

#include "logging.h"

/* Please uncomment if you want socket address definition from LWIP include file rather than local one  */
/* This is recommended if network interface uses LWIP to save some code size. This is required if      */
/* project uses IPv6 */

/* #define NET_USE_LWIP_DEFINITIONS  */

/* Experimental : Please uncomment if you want to use only control part of network library              */
/* net_socket APIs are directly redefined to LWIP, NET_MBEDTLS_HOST_SUPPORT is not supported with        */
/* this mode, dedicated to save memory (4K code)                                                        */
/* #define NET_BYPASS_NET_SOCKET */


/* Please uncomment if secure socket have to be supported and is implemented thanks to MBEDTLS running on MCU */
/* #define NET_MBEDTLS_HOST_SUPPORT */

/* Please uncomment if device supports internally Secure TCP connection */
/* #define NET_MBEDTLS_DEVICE_SUPPORT */
#define NET_USE_RTOS

/* Please uncomment if dhcp server is required and not natively supported by network interface */
/* #define NET_DHCP_SERVER_HOST_SUPPORT*/

/* when using LWIP , size of hostname */
#define NET_IP_HOSTNAME_MAX_LEN        32

#define NET_USE_IPV6                    0

#if NET_USE_IPV6 && !defined(NET_USE_LWIP_DEFINITIONS)
#error "NET IPV6 required to define NET_USE_LWIP_DEFINTIONS"
#endif /* NET_USE_IPV6 */


#define NET_MAX_SOCKETS_NBR             8

#define NET_IF_NAME_LEN                 128
#define NET_DEVICE_NAME_LEN             64
#define NET_DEVICE_ID_LEN               64
#define NET_DEVICE_VER_LEN              64


#define NET_SOCK_DEFAULT_RECEIVE_TO     60000
#define NET_SOCK_DEFAULT_SEND_TO        60000
#define NET_UDP_MAX_SEND_BLOCK_TO       1024

#define NET_USE_DEFAULT_INTERFACE       1

#define NET_RTOS_SUSPEND                if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ) { vTaskSuspendAll(); }
#define NET_RTOS_RESUME                 if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ) { ( void ) xTaskResumeAll(); }

#define NET_DBG_INFO(...)               LogDebug( __VA_ARGS__ )
#define NET_DBG_ERROR(...)              LogError( __VA_ARGS__ )
#define NET_DBG_PRINT(...)              LogDebug( __VA_ARGS__ )
#define NET_ASSERT(test,...)            do { if( !( test ) ) { LogAssert( __VA_ARGS__ ); configASSERT( test ); } } while ( 0 )
#define NET_PRINT(...)                  LogInfo( __VA_ARGS__ )
#define NET_PRINT_WO_CR(...)            LogInfo( __VA_ARGS__ )
#define NET_WARNING(...)                LogWarn( __VA_ARGS__ )


#define NET_PERF_MAXTHREAD              7U

#define NET_TASK_HISTORY_SIZE           0U

#define osDelay(n)                      vTaskDelay( pdMS_TO_TICKS( n ) )


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* NET_CONF_H */
