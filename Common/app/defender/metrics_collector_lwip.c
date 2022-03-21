/*
 * FreeRTOS STM32 Reference Integration
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
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/* Standard includes. */
#include <stdint.h>

/* Interface includes. */
#include "metrics_collector.h"

/* Lwip includes. */
#include "lwip/netif.h"
#include "lwip/arch.h"
#include "lwip/stats.h"
#include "lwip/tcpip.h"         /* #define LOCK_TCPIP_CORE()     sys_mutex_lock(&lock_tcpip_core) */
#include "lwip/ip_addr.h"       /* ip_addr_t, ipaddr_ntoa, ip_addr_copy */
#include "lwip/tcp.h"           /* struct tcp_pcb */
#include "lwip/udp.h"           /* struct udp_pcb */
#include "lwip/priv/tcp_priv.h" /* tcp_listen_pcbs_t */

#include "cbor.h"

/* Lwip configuration includes. */
#include "lwipopts.h"

#if !defined( LWIP_TCPIP_CORE_LOCKING ) || ( LWIP_TCPIP_CORE_LOCKING == 0 )
#error "Network metrics are only supported in core locking mode. Please define LWIP_TCPIP_CORE_LOCKING to 1 in lwipopts.h."
#endif

#if MIB2_STATS == 0
#error "MIB2_STATS must be enabled."
#endif

#if LWIP_BYTES_IN_OUT_UNSUPPORTED != 0
#error "LWIP_BYTES_IN_OUT_UNSUPPORTED must be set to 0."
#endif

#define UINT16_STR_LEN         5
#define IPADDR_PORT_STR_LEN    ( IPADDR_STRLEN_MAX + sizeof( ':' ) + UINT16_STR_LEN + sizeof( '\0' ) )

/* Variables defined in the LWIP source code. */
extern struct tcp_pcb * tcp_active_pcbs;        /* List of all TCP PCBs that are in a state in which they accept or send data. */
extern union tcp_listen_pcbs_t tcp_listen_pcbs; /* List of all TCP PCBs in LISTEN state. */
extern struct udp_pcb * udp_pcbs;               /* List of UDP PCBs. */
extern struct netif * netif_default;
/*-----------------------------------------------------------*/

static inline CborError cbor_add_kv_uint( CborEncoder * pxEncoder,
                                          const char * pcKey,
                                          uint64_t xUInt )
{
    CborError xError = CborNoError;

    xError = cbor_encode_text_stringz( pxEncoder, pcKey );
    xError |= cbor_encode_uint( pxEncoder, xUInt );

    return xError;
}

static inline CborError cbor_add_kv_str( CborEncoder * pxEncoder,
                                         const char * pcKey,
                                         const char * pcValue )
{
    CborError xError = CborNoError;

    xError = cbor_encode_text_stringz( pxEncoder, pcKey );
    xError |= cbor_encode_text_stringz( pxEncoder, pcValue );

    return xError;
}

CborError xGetNetworkStats( CborEncoder * pxEncoder )
{
    CborError xError = CborNoError;

    if( pxEncoder == NULL )
    {
        LogError( "Invalid parameter: pxEncoder: %p", pxEncoder );
        xError = CborErrorImproperValue;
    }
    else
    {
        struct netif * pxNetif = NULL;

        uint64_t xBytesIn = 0;
        uint64_t xBytesOut = 0;
        uint64_t xPktsIn = 0;
        uint64_t xPktsOut = 0;

        CborEncoder xNSEncoder;

        xError = cbor_encode_text_stringz( pxEncoder, "ns" );
        configASSERT_CONTINUE( xError == CborNoError );

        if( xError == CborNoError )
        {
            xError = cbor_encoder_create_map( pxEncoder, &xNSEncoder, 4 );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        LOCK_TCPIP_CORE();

#if LWIP_SINGLE_NETIF
        pxNetif = netif_default;
#else
        NETIF_FOREACH( pxNetif )
#endif /* LWIP_SINGLE_NETIF */
        {
            xBytesIn += pxNetif->mib2_counters.ifinoctets;
            xBytesOut += pxNetif->mib2_counters.ifoutoctets;

            xPktsIn += pxNetif->mib2_counters.ifinnucastpkts;
            xPktsIn += pxNetif->mib2_counters.ifinucastpkts;

            xPktsOut += pxNetif->mib2_counters.ifoutucastpkts;
            xPktsOut += pxNetif->mib2_counters.ifoutnucastpkts;
        }

        UNLOCK_TCPIP_CORE();

        if( xError == CborNoError )
        {
            xError |= cbor_add_kv_uint( &xNSEncoder, "pi", xPktsIn );
            configASSERT_CONTINUE( xError == CborNoError );

            xError |= cbor_add_kv_uint( &xNSEncoder, "po", xPktsOut );
            configASSERT_CONTINUE( xError == CborNoError );

            xError = cbor_add_kv_uint( &xNSEncoder, "bi", xBytesIn );
            configASSERT_CONTINUE( xError == CborNoError );

            xError |= cbor_add_kv_uint( &xNSEncoder, "bo", xBytesOut );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( pxEncoder, &xNSEncoder );
        }
    }

    return xError;
}

/*-----------------------------------------------------------*/

static size_t xCountListeningTcpPorts( void )
{
    size_t xPortCount = 0;

    for( struct tcp_pcb_listen * pxCurPcb = tcp_listen_pcbs.listen_pcbs; pxCurPcb != NULL; pxCurPcb = pxCurPcb->next )
    {
        if( pxCurPcb->state == LISTEN )
        {
            xPortCount++;
        }
    }

    return xPortCount;
}

/*-----------------------------------------------------------*/

static char * pcGetNetifName( uint8_t ucNetifIdx,
                              char * pcNetifBuf )
{
    char * pcNetifNameFound = NULL;

    /* netif_idx == 0 means no specific interface or the default interface */
    if( ucNetifIdx == 0 )
    {
        if( netif_default != NULL )
        {
            pcNetifNameFound = netif_index_to_name( netif_default->num, pcNetifBuf );
        }
        else
        {
            strcpy( pcNetifBuf, "any" );
            pcNetifBuf[ 3 ] = '\0';
            pcNetifNameFound = pcNetifBuf;
        }
    }
    else
    {
        pcNetifNameFound = netif_index_to_name( ucNetifIdx, pcNetifBuf );
    }

    return pcNetifNameFound;
}

/*-----------------------------------------------------------*/

static CborError xAppendTcpPtsToList( CborEncoder * pxPTSEncoder )
{
    CborError xError = CborNoError;

    configASSERT( pxPTSEncoder != NULL );

    for( struct tcp_pcb_listen * pxCurPcb = tcp_listen_pcbs.listen_pcbs; pxCurPcb != NULL; pxCurPcb = pxCurPcb->next )
    {
        CborEncoder xPTEncoder;
        char pcNetifBuf[ NETIF_NAMESIZE ] = { 0 };
        char * pcNetifNameFound = NULL;

        pcNetifNameFound = pcGetNetifName( pxCurPcb->netif_idx, pcNetifBuf );

        if( pcNetifNameFound != NULL )
        {
            xError = cbor_encoder_create_map( pxPTSEncoder, &xPTEncoder, 2 );
            configASSERT_CONTINUE( xError == CborNoError );

            if( xError == CborNoError )
            {
                xError = cbor_add_kv_str( &xPTEncoder, "if", pcNetifNameFound );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }
        else
        {
            xError = cbor_encoder_create_map( pxPTSEncoder, &xPTEncoder, 1 );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_add_kv_uint( &xPTEncoder, "pt", pxCurPcb->local_port );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( pxPTSEncoder, &xPTEncoder );
        }
    }

    return xError;
}

/*-----------------------------------------------------------*/

CborError xGetListeningTcpPorts( CborEncoder * pxMetricsEncoder )
{
    CborError xError = CborNoError;
    uint32_t ulPortCount = 0;

    if( pxMetricsEncoder == NULL )
    {
        LogError( "Invalid parameter: pxMetricsEncoder: %p", pxMetricsEncoder );
        xError = CborErrorImproperValue;
    }
    else
    {
        CborEncoder xTPEncoder;
        CborEncoder xPTSEncoder;

        LOCK_TCPIP_CORE();
        ulPortCount = xCountListeningTcpPorts();

        xError = cbor_encode_text_stringz( pxMetricsEncoder, "tp" );
        configASSERT_CONTINUE( xError == CborNoError );

        if( xError == CborNoError )
        {
            /* Create listening_tcp_ports / tp object */
            if( ulPortCount > 0 )
            {
                xError = cbor_encoder_create_map( pxMetricsEncoder, &xTPEncoder, 2 );
                configASSERT_CONTINUE( xError == CborNoError );
            }
            else
            {
                xError = cbor_encoder_create_map( pxMetricsEncoder, &xTPEncoder, 1 );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }

        /* Encode number of ports parameter */
        if( xError == CborNoError )
        {
            xError = cbor_add_kv_uint( &xTPEncoder, "t", ulPortCount );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        /* Construct ports list / pts if any tcp ports are listening */
        if( ulPortCount > 0 )
        {
            if( xError == CborNoError )
            {
                xError = cbor_encode_text_stringz( &xTPEncoder, "pts" );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = cbor_encoder_create_array( &xTPEncoder, &xPTSEncoder, ulPortCount );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = xAppendTcpPtsToList( &xPTSEncoder );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = cbor_encoder_close_container( &xTPEncoder, &xPTSEncoder );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( pxMetricsEncoder, &xTPEncoder );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        UNLOCK_TCPIP_CORE();
    }

    return xError;
}
/*-----------------------------------------------------------*/

static size_t xCountListeningUdpPorts( void )
{
    size_t xPortCount = 0;

    for( struct udp_pcb * pxCurPcb = udp_pcbs; pxCurPcb != NULL; pxCurPcb = pxCurPcb->next )
    {
        xPortCount++;
    }

    return xPortCount;
}

static CborError xAppendUdpPtsToList( CborEncoder * pxPTSEncoder )
{
    CborError xError = CborNoError;

    configASSERT( pxPTSEncoder != NULL );

    for( struct udp_pcb * pxCurPcb = udp_pcbs; pxCurPcb != NULL; pxCurPcb = pxCurPcb->next )
    {
        CborEncoder xPTEncoder;
        char pcNetifBuf[ NETIF_NAMESIZE ] = { 0 };
        char * pcNetifName = NULL;

        pcNetifName = pcGetNetifName( pxCurPcb->netif_idx, pcNetifBuf );

        if( pcNetifName != NULL )
        {
            xError = cbor_encoder_create_map( pxPTSEncoder, &xPTEncoder, 2 );
            configASSERT_CONTINUE( xError == CborNoError );

            if( xError == CborNoError )
            {
                xError = cbor_add_kv_str( &xPTEncoder, "if", pcNetifName );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }
        else
        {
            xError = cbor_encoder_create_map( pxPTSEncoder, &xPTEncoder, 1 );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_add_kv_uint( &xPTEncoder, "pt", pxCurPcb->local_port );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( pxPTSEncoder, &xPTEncoder );
        }
    }

    return xError;
}



CborError xGetListeningUdpPorts( CborEncoder * pxMetricsEncoder )
{
    CborError xError = CborNoError;
    uint32_t ulPortCount = 0;

    if( pxMetricsEncoder == NULL )
    {
        LogError( "Invalid parameter: pxMetricsEncoder: %p", pxMetricsEncoder );
        xError = CborErrorImproperValue;
    }
    else
    {
        CborEncoder xUPEncoder;
        CborEncoder xPTSEncoder;

        LOCK_TCPIP_CORE();
        ulPortCount = xCountListeningUdpPorts();

        xError = cbor_encode_text_stringz( pxMetricsEncoder, "up" );
        configASSERT_CONTINUE( xError == CborNoError );

        if( xError == CborNoError )
        {
            /* Create listening_udp_ports / up object */
            if( ulPortCount > 0 )
            {
                xError = cbor_encoder_create_map( pxMetricsEncoder, &xUPEncoder, 2 );
                configASSERT_CONTINUE( xError == CborNoError );
            }
            else
            {
                xError = cbor_encoder_create_map( pxMetricsEncoder, &xUPEncoder, 1 );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }

        /* Encode number of ports parameter */
        if( xError == CborNoError )
        {
            xError = cbor_add_kv_uint( &xUPEncoder, "t", ulPortCount );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        /* Construct ports list / pts if any udp ports are listening */
        if( ulPortCount > 0 )
        {
            if( xError == CborNoError )
            {
                xError = cbor_encode_text_stringz( &xUPEncoder, "pts" );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = cbor_encoder_create_array( &xUPEncoder, &xPTSEncoder, ulPortCount );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = xAppendUdpPtsToList( &xPTSEncoder );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = cbor_encoder_close_container( &xUPEncoder, &xPTSEncoder );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( pxMetricsEncoder, &xUPEncoder );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        UNLOCK_TCPIP_CORE();
    }

    return xError;
}
/*-----------------------------------------------------------*/


static size_t xCountTcpConnections( void )
{
    size_t xConnectionCount = 0;

    for( struct tcp_pcb * pxCurPcb = tcp_active_pcbs; pxCurPcb != NULL; pxCurPcb = pxCurPcb->next )
    {
        xConnectionCount++;
    }

    return xConnectionCount;
}

static bool xIpAddrPortToString( char * pcBuffer,
                                 size_t xBuffLen,
                                 ip_addr_t * pxIpAddr,
                                 uint16_t usPort )
{
    bool xReturn = false;

    if( ipaddr_ntoa_r( pxIpAddr, pcBuffer, xBuffLen ) != NULL )
    {
        size_t xLen = strnlen( pcBuffer, xBuffLen );
        xLen += snprintf( &( pcBuffer[ xLen ] ), xBuffLen - xLen, ":%hu", usPort );

        if( xLen <= xBuffLen )
        {
            xReturn = true;
        }
    }

    configASSERT_CONTINUE( xReturn );
    return xReturn;
}

static CborError xAppendTcpConnectionsToList( CborEncoder * pxCSEncoder )
{
    CborError xError = CborNoError;

    configASSERT( pxCSEncoder != NULL );

    for( struct tcp_pcb * pxCurPcb = tcp_active_pcbs; pxCurPcb != NULL; pxCurPcb = pxCurPcb->next )
    {
        CborEncoder xCEncoder;
        char pcRemoteIpBuf[ IPADDR_PORT_STR_LEN ] = { 0 };
        char pcNetifBuf[ NETIF_NAMESIZE ] = { 0 };
        char * pcNetifName = NULL;

        xError = cbor_encoder_create_map( pxCSEncoder, &xCEncoder, 3 );
        configASSERT_CONTINUE( xError == CborNoError );

        /* Add remote ip / port attribute */
        if( xError == CborNoError )
        {
            if( xIpAddrPortToString( pcRemoteIpBuf, IPADDR_PORT_STR_LEN, &( pxCurPcb->remote_ip ), pxCurPcb->remote_port ) )
            {
                xError = cbor_add_kv_str( &xCEncoder, "rad", pcRemoteIpBuf );
                configASSERT_CONTINUE( xError == CborNoError );
            }
            else
            {
                xError = CborUnknownError;
            }
        }

        pcNetifName = pcGetNetifName( pxCurPcb->netif_idx, pcNetifBuf );

        /* add local interface attribute */
        if( pcNetifName != NULL )
        {
            if( xError == CborNoError )
            {
                xError = cbor_add_kv_str( &xCEncoder, "li", pcNetifName );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }
        else
        {
            xError = CborUnknownError;
            configASSERT_CONTINUE( xError == CborNoError );
        }

        /* Add local port attribute */
        if( xError == CborNoError )
        {
            xError = cbor_add_kv_uint( &xCEncoder, "lp", pxCurPcb->local_port );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( pxCSEncoder, &xCEncoder );
        }
    }

    return xError;
}


CborError xGetEstablishedConnections( CborEncoder * pxMetricsEncoder )
{
    CborError xError = CborNoError;
    uint32_t ulConnCount = 0;

    if( pxMetricsEncoder == NULL )
    {
        LogError( "Invalid parameter: pxMetricsEncoder: %p", pxMetricsEncoder );
        xError = CborErrorImproperValue;
    }
    else
    {
        CborEncoder xTCEncoder; /* tc object */
        CborEncoder xECEncoder; /* ec object */
        CborEncoder xCSEncoder; /* cs list */

        LOCK_TCPIP_CORE();
        ulConnCount = xCountTcpConnections();

        xError = cbor_encode_text_stringz( pxMetricsEncoder, "tc" );
        configASSERT_CONTINUE( xError == CborNoError );

        if( xError == CborNoError )
        {
            /* Create tcp_connections / tc object */
            xError = cbor_encoder_create_map( pxMetricsEncoder, &xTCEncoder, 1 );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_encode_text_stringz( &xTCEncoder, "ec" );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            /* Create established_connections / ec object */
            if( ulConnCount > 0 )
            {
                xError = cbor_encoder_create_map( &xTCEncoder, &xECEncoder, 2 );
                configASSERT_CONTINUE( xError == CborNoError );
            }
            else
            {
                xError = cbor_encoder_create_map( &xTCEncoder, &xECEncoder, 1 );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }

        /* Encode number of connections parameter */
        if( xError == CborNoError )
        {
            xError = cbor_add_kv_uint( &xECEncoder, "t", ulConnCount );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        /* Construct connections_list / cs if any tcp ports are connected */
        if( ulConnCount > 0 )
        {
            if( xError == CborNoError )
            {
                xError = cbor_encode_text_stringz( &xECEncoder, "cs" );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = cbor_encoder_create_array( &xECEncoder, &xCSEncoder, ulConnCount );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = xAppendTcpConnectionsToList( &xCSEncoder );
                configASSERT_CONTINUE( xError == CborNoError );
            }

            if( xError == CborNoError )
            {
                xError = cbor_encoder_close_container( &xECEncoder, &xCSEncoder );
                configASSERT_CONTINUE( xError == CborNoError );
            }
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( &xTCEncoder, &xECEncoder );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        if( xError == CborNoError )
        {
            xError = cbor_encoder_close_container( pxMetricsEncoder, &xTCEncoder );
            configASSERT_CONTINUE( xError == CborNoError );
        }

        UNLOCK_TCPIP_CORE();
    }

    return xError;
}

/*-----------------------------------------------------------*/
