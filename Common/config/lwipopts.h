/**
 ******************************************************************************
 * @file    lwipopts.h
 * @author  MCD Application Team
 * @brief   Header for lwip app configuration file
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 */

#ifndef LWIP_HDR_LWIPOPTS_H
#define LWIP_HDR_LWIPOPTS_H

#include "lwipopts_freertos.h"

/*#define LWIP_DEBUG        1 */
/* #define DHCP_DEBUG     LWIP_DBG_ON */
/* #define ETHARP_DEBUG     LWIP_DBG_ON */
/* #define SOCKETS_DEBUG     LWIP_DBG_ON */
/* #define TCP_DEBUG         LWIP_DBG_ON */
/* #define UDP_DEBUG         LWIP_DBG_ON */
/* #define IP_DEBUG          LWIP_DBG_ON */
/* #define MEM_DEBUG         LWIP_DBG_ON */
/*#define PBUF_DEBUG          LWIP_DBG_ON */

/*#define LWIP_IPV6                       1 */
/*#define LWIP_IPV6_DHCP6                 1 */
#define LWIP_DHCP                             1
#define LWIP_DNS                              1
#define LWIP_SO_SNDTIMEO                      1
#define LWIP_SO_RCVTIMEO                      1
#define LWIP_SO_SNDRCVTIMEO_NONSTANDARD       1
#define LWIP_SO_RCVRCVTIMEO_NONSTANDARD       1
#define LWIP_TCPIP_CORE_LOCKING               1
#define LWIP_ARP                              1
#define LWIP_STATS                            1
#define MIB2_STATS                            1
#define LWIP_POSIX_SOCKETS_IO_NAMES           0
#define LWIP_COMPAT_SOCKETS                   2

#define LWIP_TCP_KEEPALIVE                    1  /* Keep the TCP link active. Important for MQTT/TLS */
#define LWIP_RANDOMIZE_INITIAL_LOCAL_PORTS    1  /* Prevent the same port to be used after reset.
                                                  * //                                                   Otherwise, the remote host may be confused if the port was not explicitly closed before the reset. */

/*#define TCP_LISTEN_BACKLOG            1 */

#define LWIP_TIMEVAL_PRIVATE    0

/**
 * NO_SYS==1: Provides VERY minimal functionality. Otherwise,
 * use lwIP facilities.
 */
#define NO_SYS                  0

/* ---------- link callback options ---------- */

/* LWIP_NETIF_LINK_CALLBACK==1: Support a callback function from an interface
 * whenever the link changes (i.e., link down)
 */
#define LWIP_NETIF_LINK_CALLBACK      1
#define LWIP_NETIF_STATUS_CALLBACK    1

/*
 * ------------------------------------
 * ---------- Socket options ----------
 * ------------------------------------
 */

/**
 * LWIP_SOCKET==1: Enable Socket API (require to use sockets.c)
 */
/* Change next define to support socket interface */
#define LWIP_SOCKET    1

/*#define MEMP_NUM_TCP_PCB                5 */

/*
 * -----------------------------------
 * ---------- DEBUG options ----------
 * -----------------------------------
 */

/* #define LWIP_DEBUG */

/*
 * ---------------------------------
 * ---------- OS options ----------
 * ---------------------------------
 */

/*#define TCPIP_THREAD_NAME              "TCP/IP" */
/*#define TCPIP_THREAD_STACKSIZE          (4096U) */
/*#define TCPIP_THREAD_PRIO               (24) */
/*#define TCPIP_MBOX_SIZE                 20 */
/*#define DEFAULT_UDP_RECVMBOX_SIZE       10 */
/*#define DEFAULT_TCP_RECVMBOX_SIZE       20 */
/*#define DEFAULT_ACCEPTMBOX_SIZE         10 */
#define DEFAULT_THREAD_STACKSIZE    2048
#define LWIP_COMPAT_MUTEX           0

#define MEM_ALIGNMENT               8
/*#define MIN_SIZE        8 */
/*#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) u32_t variable_name[(size + sizeof(u32_t) - 1) / sizeof(u32_t)] */
/* ---------- Memory options ---------- */

#define MEM_LIBC_MALLOC    ( 0 )
#define MEMP_MEM_MALLOC    ( 0 )
/*#define MEM_SIZE                        (50*1600) */
#define MEM_ALIGNMENT      8

/* ---------- TCP options ---------- */
#define LWIP_TCP           1
#define TCP_TTL            255

/* Controls if TCP should queue segments that arrive out of
 * order. Define to 0 if your device is low on memory. */
#define TCP_QUEUE_OOSEQ    1

/*  TCP_SND_QUEUELEN: TCP sender buffer space (pbufs). This must be at least
 * as much as (2 * TCP_SND_BUF/TCP_MSS) for things to work. */

#define TCP_SND_QUEUELEN    ( 4 * TCP_SND_BUF / TCP_MSS )

/* TCP receive window. */
#define PBUF_POOL_SIZE      40


#define TCP_MSL             20 * 1000UL /* The maximum segment lifetime in milliseconds */

/* ---------- ICMP options ---------- */
#define LWIP_SO_RCVTIMEO    1             /* ICPM PING */
#define LWIP_ICMP           1
#define LWIP_RAW            1             /* PING changed to 1 */
/*#define DEFAULT_RAW_RECVMBOX_SIZE       3 / * for ICMP PING * / */

/* To use single transmit pbuf ,this may be more efficient for MXCHIP */
/*#define LWIP_NETIF_TX_SINGLE_PBUF 1 */
/*#define TCP_OVERSIZE              1 */
/* when allocating buffer for MXCHIP , an header must be provisionned for TX buffers , default is zero */
#define PBUF_LINK_ENCAPSULATION_HLEN    28
#endif /* LWIP_HDR_LWIPOPTS_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
