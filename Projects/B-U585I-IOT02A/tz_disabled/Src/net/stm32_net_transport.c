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
 */

#include "logging_levels.h"

#define LOG_LEVEL LOG_DEBUG

#include "logging.h"
#include <assert.h>
#include <stm32_net_transport.h>

#include "net_connect.h"

#include "FreeRTOS.h"

#ifdef AF_UNSPEC
static_assert( AF_UNSPEC == NET_AF_UNSPEC );
#else
#error "AF_UNSPEC is not defined"
#endif

#ifdef AF_INET
static_assert( AF_INET == NET_AF_INET );
#else
#error "AF_INET is not defined"
#endif

#ifndef AF_INET6
#error "AF_INET6 is not defined"
#endif

static_assert( SOCK_STREAM == NET_SOCK_STREAM );
static_assert( SOCK_DGRAM == NET_SOCK_DGRAM );
static_assert( SOCK_RAW == NET_SOCK_RAW );
static_assert( IPPROTO_IP == NET_IPPROTO_IP );
static_assert( IPPROTO_ICMP == NET_IPPROTO_ICMP );
static_assert( IPPROTO_TCP == NET_IPPROTO_TCP );
static_assert( IPPROTO_UDP == NET_IPPROTO_UDP );
static_assert( SO_SNDTIMEO == NET_SO_SNDTIMEO );
static_assert( SO_RCVTIMEO == NET_SO_RCVTIMEO );
static_assert( SOCK_OK == NET_OK );


typedef struct SocketContext
{
    uint32_t ulSocket;
    uint8_t  family;
} SocketContext_t;

/*
 * @brief Allocate a new socket
 * @param domain[in] Should be AF_INET or AF_INET6
 * @param type[in] Should be one of SOCK_STREAM, SOCK_DGRAM, or SOCK_RAW
 * @param protocol[in] Should be IPPROTO_TCP or IPPROTO_UDP
 * @return a pointer to a heap-allocated NetworkContext_t.
 */
static NetworkContext_t * stm32_transport_allocate( uint8_t family,
                                                    int32_t type,
                                                    int32_t protocol )
{
    SocketContext_t * pxSocketContext = pvPortMalloc( sizeof( SocketContext_t ) );

    if( pxSocketContext != NULL )
    {
        pxSocketContext->ulSocket = net_socket( family, type, protocol );
        if( pxSocketContext->ulSocket < 0 )
        {
            LogError("Error while allocating socket.");
            vPortFree( pxSocketContext );
            pxSocketContext = NULL;
        }
        else
        {
            pxSocketContext->family = family;
        }
    }
    else
    {
        LogError("Error while allocating memory for NetworkContext_t.");
    }

    return ( NetworkContext_t * ) pxSocketContext;
}

/*-----------------------------------------------------------*/

static int32_t stm32_transport_setsockopt( NetworkContext_t * pxNetworkContext,
                                           int32_t sockopt,
                                           const void *sockopt_value,
                                           uint32_t option_len )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    // This seems to be broken.
    return 0;

    if( pxNetworkContext == NULL )
    {
        LogError( "Provided pxNetworkContext is NULL." );
        lReturnValue = -EINVAL;
    }
    else if( sockopt_value == NULL && option_len != 0 )
    {
        LogError( "Provided option_value is NULL, but option_len is non-zero: %d.", option_len );
        lReturnValue = -EINVAL;
    }
    else if( sockopt != SO_SNDTIMEO &&
             sockopt != SO_RCVTIMEO )
    {
        LogError( "Invalid socket option provided: %d.", sockopt );
        lReturnValue = -ENOTSUP;
    }
    else
    {
        lReturnValue = net_setsockopt( pxSocketContext->ulSocket,
                                       NET_SOL_SOCKET,
                                       sockopt,
                                       sockopt_value,
                                       option_len );
        if( lReturnValue != NET_OK )
        {
            LogError( "Error during setsockopt operation: %d.", sockopt );
        }
    }
    return lReturnValue;
}


/*
 * @brief Connect socket to a given address.
 */
static int32_t stm32_transport_connect( NetworkContext_t * pxNetworkContext,
                                        sockaddr_t * pSocketAddress )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext == NULL )
    {
        LogError("Provided pxNetworkContext is NULL.");
        lReturnValue = -EINVAL;
    }
    else if( pSocketAddress == NULL )
    {
        LogError("Provided pSockAddr is NULL.");
        lReturnValue = -EINVAL;
    }
    else
    {
        lReturnValue = net_connect( pxSocketContext->ulSocket, pSocketAddress, pSocketAddress->sa_len );
    }

    return lReturnValue;
}

/*-----------------------------------------------------------*/

/*
 * @brief Connect socket to a given hostname.
 */
static int32_t stm32_transport_connect_name( NetworkContext_t * pxNetworkContext,
                                             const char * pHostName,
                                             uint16_t port )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    /* This won't work if NET_USE_DEFAULT_INTERFACE is not defined */
    static_assert( NET_USE_DEFAULT_INTERFACE );

    sockaddr_t addr;
    memset( &addr, 0, sizeof( sockaddr_t ) );

    if( pxNetworkContext == NULL )
    {
        LogError("Provided pxNetworkContext is NULL.");
        lReturnValue = -EINVAL;
    }
    else
    {
        /* Workaround.. Trust the network lib not to do modify pHostName */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
        lReturnValue = net_if_gethostbyname( NULL, &addr, (char *) pHostName );
#pragma GCC diagnostic pop

        if( lReturnValue == 0 &&
            addr.sa_family == pxSocketContext->family )
        {
            /* gethostbyname should only return AF_INET or AF_INET6 */
            configASSERT( ( addr.sa_family == AF_INET ) ||
                          ( addr.sa_family == AF_INET6 ) );

            /* Note: v4 and v6 port field offsets are identical. */

            sockaddr_in_t *addr_v4 = (sockaddr_in_t *) &addr;

            addr_v4->sin_port = htons(port);

            lReturnValue = stm32_transport_connect( pxNetworkContext, &addr );
        }
        else
        {
            LogError( "Call to gethostbyname failed with code: %d", lReturnValue );
        }
    }

    return lReturnValue;
}

/*-----------------------------------------------------------*/

static int32_t stm32_transport_close( NetworkContext_t * pxNetworkContext )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext != NULL )
    {
        lReturnValue = net_closesocket( pxSocketContext->ulSocket );
        vPortFree( pxSocketContext );
    }
    else
    {
        LogError("Provided pxNetworkContext is NULL.");
        lReturnValue = -EINVAL;
    }

    return lReturnValue;
}

/*-----------------------------------------------------------*/

static int32_t stm32_transport_send( NetworkContext_t * pxNetworkContext,
                                     const void * pBuffer,
                                     size_t xBytesToSend )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext != NULL )
    {
        /* Workaround.. Trust the network lib not to do modify pBuffer */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
        lReturnValue = net_send( pxSocketContext->ulSocket, (void *) pBuffer, xBytesToSend, 0 );
#pragma GCC diagnostic pop

    }
    else
    {
        LogError("Provided pxNetworkContext is NULL.");
        lReturnValue = -EINVAL;
    }
    return lReturnValue;
}

/*-----------------------------------------------------------*/

static int32_t stm32_transport_recv( NetworkContext_t * pxNetworkContext,
                                     void * pBuffer,
                                     size_t xBytesToRecv )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext != NULL )
    {
        lReturnValue = net_recv( pxSocketContext->ulSocket, pBuffer, xBytesToRecv, 0 );
    }
    else
    {
        LogError("Provided pxNetworkContext is NULL.");
        lReturnValue = -EINVAL;
    }
    return lReturnValue;
}
/*-----------------------------------------------------------*/

/* Export extended transport interface */

const TransportInterfaceExtended_t xSTM32TransportInterface =
{
    .recv = stm32_transport_recv,
    .send = stm32_transport_send,
    .socket = stm32_transport_allocate,
    .setsockopt = stm32_transport_setsockopt,
    .connect = stm32_transport_connect,
    .connect_name = stm32_transport_connect_name,
    .close = stm32_transport_close
};
