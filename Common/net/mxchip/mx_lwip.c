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
#include "logging_levels.h"

#define LOG_LEVEL    LOG_ERROR

#include "logging.h"

#include "mx_lwip.h"

#include "FreeRTOS.h"
#include "atomic.h"
#include "mx_prv.h"

static void vAddMXHeaderToEthernetFrame( PacketBuffer_t * pxTxPacket )
{
    configASSERT( pxTxPacket != NULL );

    /* Store length of ethernet frame for BypassInOut_t header */
    uint16_t ulEthPacketLen = pxTxPacket->tot_len;

    /* Adjust pbuf size to include BypassInOut_t header */
    uint8_t cErr = pbuf_header( pxTxPacket, sizeof( BypassInOut_t ) );

    configASSERT( cErr == 0 );

    /* Add on bypass header */
    BypassInOut_t * pxBypassHeader = ( BypassInOut_t * ) pxTxPacket->payload;

    pxBypassHeader->xHeader.usIPCApiId = IPC_WIFI_BYPASS_OUT;
    pxBypassHeader->xHeader.ulIPCRequestId = prvGetNextRequestID();

    /* Send to station interface */
    pxBypassHeader->lIndex = WIFI_BYPASS_MODE_STATION;

    /* Fill pad region with zeros */
    ( void ) memset( pxBypassHeader->ucPad, 0, MX_BYPASS_PAD_LEN );

    /* Set length field */
    pxBypassHeader->usDataLen = ulEthPacketLen;

    configASSERT( pxTxPacket->ref >= 1 );
}

/* Callback for lwip netif events
 * netif_set_status_callback metif_set_link_callback */
static void vLwipStatusCallback( struct netif * pxNetif )
{
    static ip_addr_t xLastAddr = { 0 };
    static uint8_t xLastFlags = 0;

    MxNetConnectCtx_t * pxCtx = ( MxNetConnectCtx_t * ) pxNetif->state;

    LogDebug( "vLwipStatusCallback called" );

    uint32_t ulNotifyValue = 0;

    /* Check for change in flags */
    if( ( pxNetif->flags ^ xLastFlags ) & NETIF_FLAG_UP )
    {
        if( pxNetif->flags & NETIF_FLAG_UP )
        {
            ulNotifyValue |= NET_LWIP_IFUP_BIT;
        }
        else
        {
            ulNotifyValue |= NET_LWIP_IFDOWN_BIT;
        }
    }
    else if( ( pxNetif->flags ^ xLastFlags ) & NETIF_FLAG_LINK_UP )
    {
        if( pxNetif->flags & NETIF_FLAG_LINK_UP )
        {
            ulNotifyValue |= NET_LWIP_LINK_UP_BIT;
        }
        else
        {
            ulNotifyValue |= NET_LWIP_LINK_DOWN_BIT;
        }
    }

    if( pxNetif->ip_addr.addr != xLastAddr.addr )
    {
        ulNotifyValue |= NET_LWIP_IP_CHANGE_BIT;
    }

    if( ulNotifyValue > 0 )
    {
        ( void ) xTaskNotifyIndexed( pxCtx->xNetTaskHandle,
                                     NET_EVT_IDX,
                                     ulNotifyValue,
                                     eSetBits );
    }

    xLastAddr = pxNetif->ip_addr;
    xLastFlags = pxNetif->flags;
}

/* Network output function for lwip */
err_t prvxLinkOutput( NetInterface_t * pxNetif,
                      PacketBuffer_t * pxPbuf )
{
    err_t xError = ERR_OK;
    BaseType_t xReturn = pdFALSE;
    struct pbuf * pxPbufToSend = pxPbuf;

    if( ( pxPbuf == NULL ) || ( pxNetif == NULL ) )
    {
        xError = ERR_VAL;
    }
    /* Handle any chained packets */
    else if( ( pxPbuf->len != pxPbuf->tot_len ) ||
             ( pxPbuf->next != NULL ) )
    {
        pxPbufToSend = pbuf_clone( PBUF_RAW, PBUF_RAM, pxPbuf );

        if( pxPbufToSend == NULL )
        {
            xError = ERR_MEM;
        }

        /* Input buffer will be freed by lwip after the current function returns */
        /* pbuf_clone sets the refcount = 1 upon creation */
    }
    else
    {
        /* Increment reference counter */
        pbuf_ref( pxPbufToSend );
    }

/*    vPrintBuffer("ETH_TX", pxPbuf->payload, pxPbuf->tot_len ); */

    /* Get context from netif struct */
    MxNetConnectCtx_t * pxCtx = ( MxNetConnectCtx_t * ) pxNetif->state;

    if( xError == ERR_OK )
    {
        vAddMXHeaderToEthernetFrame( pxPbuf );
    }

    configASSERT( pxCtx->xDataPlaneSendQueue != NULL );
    configASSERT( pxCtx->pulTxPacketsWaiting != NULL );
    configASSERT( pxCtx->xDataPlaneTaskHandle != NULL );

    if( xError == ERR_OK )
    {
        configASSERT( pxPbufToSend != NULL );
        xReturn = xQueueSend( pxCtx->xDataPlaneSendQueue,
                              &pxPbufToSend,
                              MX_ETH_PACKET_ENQUEUE_TIMEOUT );

        if( xReturn == pdTRUE )
        {
            xError = ERR_OK;
            LogDebug( "Packet enqueued into xDataPlaneSendQueue addr: %p, len: %d, refs: %d, remaining space: %d",
                      pxPbufToSend, pxPbufToSend->tot_len, pxPbufToSend->ref, uxQueueSpacesAvailable( pxCtx->xDataPlaneSendQueue ) );

            ( void ) Atomic_Increment_u32( pxCtx->pulTxPacketsWaiting );

            ( void ) xTaskNotifyGiveIndexed( pxCtx->xDataPlaneTaskHandle, DATA_WAITING_IDX );
        }
        else
        {
            xError = ERR_TIMEOUT;
            PBUF_FREE( pxPbufToSend );
        }
    }

    return xError;
}

BaseType_t prvxLinkInput( NetInterface_t * pxNetif,
                          PacketBuffer_t * pxPbufIn )
{
    BaseType_t xReturn;

    if( pxNetif == NULL )
    {
        LogError( "pxNetif is null." );
        xReturn = pdFALSE;
    }
    else if( pxPbufIn == NULL )
    {
        LogError( "pxPbufIn is null." );
        xReturn = pdFALSE;
    }
    else if( ( ( pxNetif->flags & NETIF_FLAG_UP ) > 0 ) &&
             ( pxNetif->input != NULL ) )
    {
        struct eth_hdr * pxEthHeader = ( struct eth_hdr * ) pxPbufIn->payload;

        /* Filter by ethertype */
        uint16_t usEthertype = lwip_htons( pxEthHeader->type );

        switch( usEthertype )
        {
            case ETHTYPE_IP:
/*            vPrintBuffer("ETH_RX", pxPbufIn->payload, pxPbufIn->tot_len ); */
            /* intentional fall through */
            case ETHTYPE_IPV6:
            case ETHTYPE_ARP:

                if( pxNetif->input( pxPbufIn, pxNetif ) != ERR_OK )
                {
                    xReturn = pdFALSE;
                }
                else
                {
                    xReturn = pdTRUE;
                }

                break;

            default:
                LogDebug( "Dropping input packet with ethertype %d", usEthertype );
                xReturn = pdFALSE;
                break;
        }
    }
    else
    {
        xReturn = pdFALSE;
    }

    return xReturn;
}

/* Initialize network interface struct */
err_t prvInitNetInterface( NetInterface_t * pxNetif )
{
    configASSERT( pxNetif != NULL );

    /* Get context from netif struct */
    MxNetConnectCtx_t * pxCtx = ( MxNetConnectCtx_t * ) pxNetif->state;

    pxNetif->output = &etharp_output;
    pxNetif->linkoutput = &prvxLinkOutput;

    pxNetif->name[ 0 ] = 'm';
    pxNetif->name[ 1 ] = 'x';

    pxNetif->num = 0;
    pxNetif->mtu = ( uint16_t ) MX_MAX_MTU;

    pxNetif->hwaddr_len = ETHARP_HWADDR_LEN;

    pxNetif->flags = ( NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET );

    netif_set_status_callback( pxNetif, vLwipStatusCallback );
    netif_set_link_callback( pxNetif, vLwipStatusCallback );

    /* Set a dummy mac address */
    ( void ) memcpy( &( pxNetif->hwaddr ), &( pxCtx->xMacAddress ), ETHARP_HWADDR_LEN );

    return ERR_OK;
}
