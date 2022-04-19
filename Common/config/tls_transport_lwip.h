/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

#ifndef TLS_TRANSPORT_LWIP
#define TLS_TRANSPORT_LWIP

/* Lwip related definitions */

#define sock_socket         lwip_socket
#define sock_connect        lwip_connect
#define sock_send           lwip_send
#define sock_recv           lwip_recv
#define sock_close          lwip_close
#define sock_setsockopt     lwip_setsockopt
#define sock_fcntl          lwip_fcntl
#define sock_select         lwip_select

#define dns_getaddrinfo     lwip_getaddrinfo
#define dns_freeaddrinfo    lwip_freeaddrinfo

typedef int SockHandle_t;

#endif /* TLS_TRANSPORT_LWIP */
