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

#ifndef _MXFREE_LWIP_
#define _MXFREE_LWIP_

#include "lwip/etharp.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/netifapi.h"
#include "lwip/prot/dhcp.h"

/* Define "generic" types */
typedef struct netif      NetInterface_t;
typedef struct pbuf       PacketBuffer_t;
typedef struct eth_addr   MacAddress_t;

#define PBUF_VALID( pbuf )          \
    ( ( ( pbuf ) != NULL ) &&       \
      ( ( pbuf )->next == NULL ) && \
      ( ( pbuf )->len > 0 ) &&      \
      ( ( pbuf )->len <= MX_RX_BUFF_SZ ) )

#define PBUF_LEN( buf )         ( ( buf )->len )
#define PBUF_ALLOC_RX( len )    pbuf_alloc( PBUF_RAW, len, PBUF_POOL )
#define PBUF_ALLOC_TX( len )    pbuf_alloc( PBUF_RAW, len, PBUF_RAM )
#define PBUF_FREE( pbuf )       pbuf_free( pbuf )

/* helper functions */
static inline void vLogAddress( const char * pucLabel,
                                ip_addr_t xAddress )
{
    uint8_t * pucAddrOctets = ( uint8_t * ) &( xAddress.addr );

    ( void ) pucAddrOctets;

    LogSys( "%-12s%d.%d.%d.%d", pucLabel,
            pucAddrOctets[ 0 ], pucAddrOctets[ 1 ], pucAddrOctets[ 2 ], pucAddrOctets[ 3 ] );
}

static inline void vClearAddress( NetInterface_t * pxNetif )
{
    if( ( pxNetif->ip_addr.addr != 0 ) ||
        ( pxNetif->gw.addr != 0 ) ||
        ( pxNetif->netmask.addr != 0 ) )
    {
        err_t xLwipError;
        struct ip4_addr xEmptyAddr;
        xEmptyAddr.addr = 0;
        xLwipError = netifapi_netif_set_addr( pxNetif,
                                              &xEmptyAddr,
                                              &xEmptyAddr,
                                              &xEmptyAddr );

        if( xLwipError != ERR_OK )
        {
            LogError( "Failed to clear ip address rc: %d", xLwipError );
        }
    }
}

static inline void vStartDhcp( NetInterface_t * pxNetif )
{
    /* Start DHCP if necessary */
    struct dhcp * pxDHCP = netif_dhcp_data( pxNetif );

    if( ( pxDHCP == NULL ) ||
        ( ( pxDHCP != NULL ) && ( pxDHCP->state == DHCP_STATE_OFF ) ) )
    {
        LogInfo( "Starting DHCP." );
        err_t xLwipError = netifapi_dhcp_start( pxNetif );

        if( xLwipError != ERR_OK )
        {
            LogError( "Failed to start DHCP on link rc: %d", xLwipError );
        }
    }
}

static inline void vStopDhcp( NetInterface_t * pxNetif )
{
    /* Start DHCP if necessary */
    struct dhcp * pxDHCP = netif_dhcp_data( pxNetif );

    if( ( pxDHCP != NULL ) &&
        ( pxDHCP->state != DHCP_STATE_OFF ) )
    {
        LogInfo( "Stopping DHCP." );
        err_t xLwipError = netifapi_dhcp_start( pxNetif );

        if( xLwipError != ERR_OK )
        {
            LogError( "Failed to stop DHCP on link rc: %d", xLwipError );
        }
    }
}

static inline void vSetAdminUp( NetInterface_t * pxNetif )
{
    if( pxNetif->flags & NETIF_FLAG_UP )
    {
        LogInfo( "Setting interface administrative state to UP." );

        err_t xLwipError = netifapi_netif_set_up( pxNetif );

        if( xLwipError != ERR_OK )
        {
            LogError( "Failed to set link administrative state to UP rc: %d", xLwipError );
        }
    }
}

static inline void vSetAdminDown( NetInterface_t * pxNetif )
{
    if( ( pxNetif->flags & NETIF_FLAG_UP ) == 0 )
    {
        LogInfo( "Setting interface administrative state to DOWN." );

        err_t xLwipError = netifapi_netif_set_down( pxNetif );

        if( xLwipError != ERR_OK )
        {
            LogError( "Failed to set administrative state to DOWN rc: %d", xLwipError );
        }
    }
}

static inline void vSetLinkUp( NetInterface_t * pxNetif )
{
    err_t xLwipError = netifapi_netif_set_link_up( pxNetif );

    if( xLwipError != ERR_OK )
    {
        LogError( "Failed to set link state to UP rc: %d", xLwipError );
    }
}

static inline void vSetLinkDown( NetInterface_t * pxNetif )
{
    err_t xLwipError = netifapi_netif_set_link_down( pxNetif );

    if( xLwipError != ERR_OK )
    {
        LogError( "Failed to set link state to DOWN rc: %d", xLwipError );
    }
}

err_t prvxLinkOutput( NetInterface_t * pxNetif,
                      PacketBuffer_t * pxPbuf );
BaseType_t prvxLinkInput( NetInterface_t * pxNetif,
                          PacketBuffer_t * pxPbufIn );
err_t prvInitNetInterface( NetInterface_t * pxNetif );

#endif /* _MXFREE_LWIP_ */
