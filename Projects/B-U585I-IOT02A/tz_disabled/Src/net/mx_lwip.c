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

#include "mx_lwip.h"

#include "semphr.h"
#include "mx_prv.h"

static SemaphoreHandle_t xDataMutex = NULL;

/* Network output function for lwip */
err_t prvxLinkOutput( NetInterface_t * pxNetif, PacketBuffer_t * pxPbuf )
{
    err_t xError = ERR_OK;
    BaseType_t xReturn = pdFALSE;
    struct pbuf * pxPbufToSend = pxPbuf;

    if( pxPbuf == NULL || pxNetif == NULL )
    {
        xError = ERR_VAL;
    }
    /* Handle any chained packets */
    else if( pxPbuf->len != pxPbuf->tot_len ||
             pxPbuf->next != NULL )
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

    /* Get context from netif struct */
    MxNetConnectCtx_t * pxCtx = ( MxNetConnectCtx_t * ) pxNetif->state;

    configASSERT( pxCtx->xDataPlaneSendBuff != NULL );
    configASSERT( xDataMutex != NULL );

    if( xError == ERR_OK )
    {
        xReturn = xSemaphoreTake( xDataMutex, MX_ETH_PACKET_ENQUEUE_TIMEOUT );
        if( xReturn == pdTRUE )
        {
            xReturn = xMessageBufferSend( pxCtx->xDataPlaneSendBuff,
                                          pxPbufToSend,
                                          sizeof( PacketBuffer_t * ),
                                          MX_ETH_PACKET_ENQUEUE_TIMEOUT );

            ( void ) xSemaphoreGive( xDataMutex );

            if( xReturn == pdTRUE )
            {
                xError = ERR_OK;
                LogDebug( "Packet enqueued into xDataPlaneSendBuff addr: %p, len: %d, remaining space: %d",
                          pxPbufToSend, pxPbufToSend->len, xMessageBuffRemainingSpaces( xDataPlaneSendBuff ) );
            }
            else
            {
                xError = ERR_TIMEOUT;
                pbuf_free( pxPbufToSend );
            }
        }
        else
        {
            LogError("Failed to take xDataMutex.");
            xError = ERR_TIMEOUT;
            pbuf_free( pxPbufToSend );
        }
    }

    return xError;
}

BaseType_t prvxLinkInput( NetInterface_t * pxNetif, PacketBuffer_t * pxPbufIn )
{
    BaseType_t xReturn;

    if( pxNetif == NULL )
    {
        LogError("pxNetif is null.");
        xReturn = pdFALSE;
    }
    else if( pxPbufIn == NULL )
    {
        LogError("pxPbufIn is null.");
        xReturn = pdFALSE;
    }
    else if( ( pxNetif->flags & NETIF_FLAG_UP ) > 0 &&
               pxNetif->input != NULL )
    {
        struct eth_hdr * pxEthHeader = (struct eth_hdr *) pxPbufIn->payload;

        /* Filter by ethertype */
        uint16_t usEthertype = lwip_htons( pxEthHeader->type );

        switch( usEthertype )
        {
        case ETHTYPE_IP:
        case ETHTYPE_IPV6:
        case ETHTYPE_ARP:
            if( pxNetif->input( pxPbufIn, pxNetif ) != ERR_OK )
            {
                pbuf_free( pxPbufIn );
                xReturn = pdTRUE;
            }
            break;
        default:
            LogDebug( "Dropping input packet with ethertype %d", usEthertype );
            pbuf_free( pxPbufIn );
            xReturn = pdFALSE;
            break;
        }
    }
    else
    {
        pbuf_free( pxPbufIn );
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

    if( xDataMutex == NULL )
    {
        xDataMutex = xSemaphoreCreateMutex();
    }

    pxNetif->output = etharp_output;
    pxNetif->linkoutput = &prvxLinkOutput;

    pxNetif->name[ 0 ] = 'm';
    pxNetif->name[ 1 ] = 'x';

    pxNetif->num = 0;
    pxNetif->mtu = ( uint16_t ) MX_MAX_MTU;

    pxNetif->hwaddr_len = ETHARP_HWADDR_LEN;

    pxNetif->flags = ( NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET );

    /* Set a dummy mac address */
    ( void ) memcpy( &( pxNetif->hwaddr ), &( pxCtx->xMacAddress ), ETHARP_HWADDR_LEN );

    return ERR_OK;
}
