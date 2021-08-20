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
#include "transport_interface_ext.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "FreeRTOS.h"

#ifndef AF_UNSPEC
#error "AF_UNSPEC is not defined"
#endif

#ifndef AF_INET
#error "AF_INET is not defined"
#endif

#ifndef AF_INET6
#error "AF_INET6 is not defined"
#endif


typedef struct SocketContext
{
    uint32_t ulSocket;
    struct addrinfo xAddrInfoHint;
} SocketContext_t;

/*
 * @brief Allocate a new socket
 * @param domain[in] Should be AF_INET or AF_INET6
 * @param type[in] Should be one of SOCK_STREAM, SOCK_DGRAM, or SOCK_RAW
 * @param protocol[in] Should be IPPROTO_TCP or IPPROTO_UDP
 * @return a pointer to a heap-allocated NetworkContext_t.
 */
static NetworkContext_t * transport_allocate( int32_t family,
                                              int32_t type,
                                              int32_t protocol )
{
    SocketContext_t * pxSocketContext = pvPortMalloc( sizeof( SocketContext_t ) );

    memset( &( pxSocketContext->xAddrInfoHint ), 0 , sizeof( struct addrinfo ) );

    if( pxSocketContext != NULL )
    {
        pxSocketContext->ulSocket = lwip_socket( family, type, protocol );
        if( pxSocketContext->ulSocket < 0 )
        {
            LogError("Error while allocating socket.");
            vPortFree( pxSocketContext );
            pxSocketContext = NULL;
        }
        else
        {
            pxSocketContext->xAddrInfoHint.ai_family = family;
            pxSocketContext->xAddrInfoHint.ai_socktype = type;
            pxSocketContext->xAddrInfoHint.ai_protocol = protocol;
        }
    }
    else
    {
        LogError("Error while allocating memory for NetworkContext_t.");
    }

    return ( NetworkContext_t * ) pxSocketContext;
}

/*-----------------------------------------------------------*/

static int32_t transport_setsockopt( NetworkContext_t * pxNetworkContext,
                                     int32_t lSockopt,
                                     const void * pvSockoptValue,
                                     uint32_t ulOptionLen )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext == NULL )
    {
        LogError( "Provided pxNetworkContext is NULL." );
        lReturnValue = -EINVAL;
    }
    else if( pvSockoptValue == NULL && ulOptionLen != 0 )
    {
        LogError( "Provided pvSockoptValue is NULL, but ulOptionLen is non-zero: %d.", ulOptionLen );
        lReturnValue = -EINVAL;
    }
    else
    {
        lReturnValue = lwip_setsockopt( pxSocketContext->ulSocket,
                                        SOL_SOCKET,
                                        lSockopt,
                                        pvSockoptValue,
                                        ulOptionLen );
    }

    if( lReturnValue == ENOPROTOOPT )
    {
        LogError( "Socket operation not supported: %d.", lSockopt );
    }
    else if( lReturnValue != ERR_OK )
    {
        LogError( "Error during setsockopt operation: %d.", lSockopt );
    }
    return lReturnValue;
}


/*
 * @brief Connect socket to a given address.
 */
static int32_t transport_connect( NetworkContext_t * pxNetworkContext,
                                  const struct sockaddr * pxSocketAddress )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext == NULL )
    {
        LogError("Provided pxNetworkContext is NULL.");
        lReturnValue = -EINVAL;
    }
    else if( pxSocketAddress == NULL )
    {
        LogError("Provided pxSocketAddress is NULL.");
        lReturnValue = -EINVAL;
    }
    else
    {
        lReturnValue = lwip_connect( pxSocketContext->ulSocket, pxSocketAddress, pxSocketAddress->sa_len );
    }

    return lReturnValue;
}

/*-----------------------------------------------------------*/

/*
 * @brief Connect socket to a given hostname.
 */
static int32_t transport_connect_name( NetworkContext_t * pxNetworkContext,
                                       const char * pcHostName,
                                       uint16_t port )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    struct addrinfo * pxAddrInfo = NULL;

    if( pxNetworkContext == NULL )
    {
        LogError("Provided pxNetworkContext is NULL.");
        lReturnValue = -EINVAL;
    }
    else
    {
        lReturnValue = lwip_getaddrinfo( pcHostName, NULL, &( pxSocketContext->xAddrInfoHint ), &pxAddrInfo );

        if( lReturnValue == 0 &&
            pxAddrInfo != NULL )
        {
            /* getaddrinfo should only return AF_INET or AF_INET6 */
            configASSERT( ( pxAddrInfo->ai_family == AF_INET ) ||
                          ( pxAddrInfo->ai_family == AF_INET6 ) );

            /* Note: v4 and v6 port field offsets are identical. */

            struct sockaddr_in * addr_v4 = ( struct sockaddr_in * ) pxAddrInfo->ai_addr;

            addr_v4->sin_port = lwip_htons( port );

            lReturnValue = transport_connect( pxNetworkContext, pxAddrInfo->ai_addr );
        }
        else
        {
            LogError( "Call to getaddrinfo failed with code: %d", lReturnValue );
        }

        /* Free the addrinfo memory allocated by lwip */
        if( pxAddrInfo != NULL )
        {
            lwip_freeaddrinfo( pxAddrInfo );
        }
    }

    return lReturnValue;
}

/*-----------------------------------------------------------*/

static int32_t transport_close( NetworkContext_t * pxNetworkContext )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext != NULL )
    {
        lReturnValue = lwip_close( pxSocketContext->ulSocket );
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

static int32_t transport_send( NetworkContext_t * pxNetworkContext,
                               const void * pvBuffer,
                               size_t xBytesToSend )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext != NULL )
    {
        lReturnValue = lwip_send( pxSocketContext->ulSocket, pvBuffer, xBytesToSend, 0 );

    }
    else
    {
        LogError("Provided pxNetworkContext is NULL.");
        lReturnValue = -EINVAL;
    }
    return lReturnValue;
}

/*-----------------------------------------------------------*/

static int32_t transport_recv( NetworkContext_t * pxNetworkContext,
                               void * pvBuffer,
                               size_t xBytesToRecv )
{
    SocketContext_t * pxSocketContext = ( SocketContext_t * ) pxNetworkContext;
    int32_t lReturnValue = 0;

    if( pxNetworkContext != NULL )
    {
        lReturnValue = lwip_recv( pxSocketContext->ulSocket, pvBuffer, xBytesToRecv, 0 );
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

const TransportInterfaceExtended_t xLwipTransportInterface =
{
    .recv = transport_recv,
    .send = transport_send,
    .socket = transport_allocate,
    .setsockopt = transport_setsockopt,
    .connect = transport_connect,
    .connect_name = transport_connect_name,
    .close = transport_close
};
