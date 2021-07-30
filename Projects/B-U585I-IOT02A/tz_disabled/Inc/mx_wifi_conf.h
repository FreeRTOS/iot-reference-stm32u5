/**
  ******************************************************************************
  * @file    mx_wifi_config.h, derived from mx_wifi_conf_template.h
  * @author  MCD Application Team
  * @brief   Header for mx_wifi_conf_template module
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
#ifndef MX_WIFI_CONF_H
#define MX_WIFI_CONF_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

int32_t mxwifi_probe(void **ll_drv_context);

#define MX_WIFI_USE_SPI                                                     (1)
#define MXCHIP_SPI      hspi2

#define MX_WIFI_NETWORK_BYPASS_MODE                                         (0)

#define MX_WIFI_TX_BUFFER_NO_COPY                                           (0)

/* DEBUG LOG */
#define MX_WIFI_API_DEBUG
#define MX_WIFI_IPC_DEBUG
#define MX_WIFI_HCI_DEBUG
//#define MX_WIFI_SLIP_DEBUG
//#define MX_WIFI_IO_DEBUG

#define MX_WIFI_USE_CMSIS_OS 1

#ifndef LOCK_DECLARE
#include "mx_wifi_freertos.h"
#endif /* LOCK_DECLARE */

#define DMA_ON_USE                                                           (1)


#define MX_WIFI_PRODUCT_NAME                        ("MXCHIP-WIFI")
#define MX_WIFI_PRODUCT_ID                          ("EMW3080B")

#define MX_WIFI_MTU_SIZE                            (1500)

#define MX_WIFI_BYPASS_HEADER_SIZE                  (28)  /* MX_IPC_header(6) + sizeof(bypass_in_t)(22),
                                                           * set with PBUF_LINK_ENCAPSULATION_HLEN */
#define MX_WIFI_PBUF_LINK_HLEN                      (14)  /* link header (PBUF_LINK_HLEN) set in lwip */

#if (MX_WIFI_NETWORK_BYPASS_MODE==1)
/* Use lwip PBUF_POOL_BUFSIZE = TCP_MSS + 40 + PBUF_LINK_ENCAPSULATION_HLEN + PBUF_LINK_HLEN. */
#define MX_WIFI_BUFFER_SIZE \
  (MX_WIFI_MTU_SIZE + MX_WIFI_BYPASS_HEADER_SIZE + MX_WIFI_PBUF_LINK_HLEN)
#else
#define MX_WIFI_BUFFER_SIZE                         (2500)  /* bigger buffer size */
#endif /* (MX_WIFI_NETWORK_BYPASS_MODE==1) */

/* MX_WIFI_BUFFER_SIZE - MX_IPC_header */
#define MX_WIFI_IPC_PAYLOAD_SIZE                    ((MX_WIFI_BUFFER_SIZE) - 6)

/* MX_WIFI_IPC_PAYLOAD_SIZE - socket_api_params_header */
#define MX_WIFI_SOCKET_DATA_SIZE                    ((MX_WIFI_IPC_PAYLOAD_SIZE)-12)

#define MX_WIFI_CMD_TIMEOUT                         (5000)
#define MX_WIFI_MAX_SOCKET_NBR                      (8)
#define MX_WIFI_MAX_DETECTED_AP                     (10)

#define MX_WIFI_MAX_SSID_NAME_SIZE                  32
#define MX_WIFI_MAX_PSWD_NAME_SIZE                  64

#define MX_WIFI_PRODUCT_NAME_SIZE                   32
#define MX_WIFI_PRODUCT_ID_SIZE                     32

#define MX_WIFI_FW_REV_SIZE                         24

#define MX_WIFI_SPI_THREAD_PRIORITY                 (30)
#define MX_WIFI_SPI_THREAD_STACK_SIZE               (2048)

#define MX_WIFI_RECEIVED_THREAD_PRIORITY            (16)
#define MX_WIFI_RECEIVED_THREAD_STACK_SIZE          (2048)

#define MX_WIFI_TRANSMIT_THREAD_PRIORITY            (16)
#define MX_WIFI_TRANSMIT_THREAD_STACK_SIZE          (2048)

/* Maximum number of RX buffer that can be queued by Hardware interface (SPI/UART)                         */
/* This is used to size internal queue, and avoid to block the IP thread if it can still push some buffers */
/* Impact on Memory foot print is weak , one single void* per place in the queue                           */
#define MX_WIFI_MAX_RX_BUFFER_COUNT                     (4)

/* Maximum number of TX buffer that can be queued by IP stack (LwIP or Netx) without blocking the calling thread */
/* This is used to size internal queue, and avoid to block the IP thread if it can still push some buffers       */
/* Impact on Memory foot print is one single void* per place in the queue, but it may lead to over allocation    */
/* TCP/IP stack (LwIP  for instance )                                                                            */
#define MX_WIFI_MAX_TX_BUFFER_COUNT                     (4)


/**
  * For the TX buffer, by default no-copy feature is enabled, meaning that
  * the IP buffer are used in the whole process and should come with
  * available room in front of payload to accommodate transport header buffer.
  * This is managed in interface between driver and IP stack
  */

/**
  * For LwIP "PBUF_LINK_ENCAPSULATION_HLEN" must be defined as > MX_WIFI_MIN_TX_HEADER_SIZE,
  * see net_mx_wifi/c file for implementation.
  */
#define MX_WIFI_MIN_TX_HEADER_SIZE                      (28)

#ifndef MX_STAT_ON
#define MX_STAT_ON      0
#endif /* MX_STAT_ON */

#if MX_STAT_ON == 1
typedef struct
{
  uint32_t alloc;
  uint32_t free;
  uint32_t cmd_get_answer;
  uint32_t callback;
  uint32_t in_fifo;
  uint32_t out_fifo;
} mx_stat_t;

extern mx_stat_t mx_stat;

#define MX_STAT_LOG() \
LogInfo("Number of allocated buffer for Rx and command answer %ld\n", mx_stat.alloc);\
LogInfo("Number of free buffer %ld\n",mx_stat.free);\
LogInfo("Number of command answer %ld , callback %ld, sum of both %ld (should match alloc && free)\n",\
	    mx_stat.cmd_get_answer,mx_stat.callback,mx_stat.cmd_get_answer+mx_stat.callback);\
LogInfo("Number of posted answer (callback + cmd answer) %ld, processed answer %ld\n",\
	    mx_stat.in_fifo,mx_stat.out_fifo);\

#define MX_STAT_INIT()        (void) memset((void*)&mx_stat, 0, sizeof(mx_stat))
#define MX_STAT(A)            mx_stat.A++
#define MX_STAT_DECLARE()     mx_stat_t mx_stat

#else

#define MX_STAT_INIT()
#define MX_STAT(A)
#define MX_STAT_LOG()
#define MX_STAT_DECLARE()

#endif /* MX_STAT_ON */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MX_WIFI_CONF_H */

