
#ifndef SOCKET_INTERFACE
#define SOCKET_INTERFACE


#include "transport_interface.h"
#include "lwip/sockets.h"

/*
 * Address Families
 */
#ifndef AF_UNSPEC
#define AF_UNSPEC       0
#endif

#ifndef AF_INET
#define AF_INET         2
#endif

#ifndef AF_INET6
#define AF_INET6        28
#endif

/*
 * Socket Types
 */
#ifndef SOCK_STREAM
#define SOCK_STREAM     1
#endif

#ifndef SOCK_DGRAM
#define SOCK_DGRAM      2
#endif

#ifndef SOCK_RAW
#define SOCK_RAW        3
#endif

/*
 * Socket Options
 */
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO     0x1005
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO     0x1006
#endif

/*
 * Protocols
 */
#ifndef IPPROTO_IP
#define IPPROTO_IP      0
#endif

#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP    1
#endif

#ifndef IPPROTO_TCP
#define IPPROTO_TCP     6
#endif

#ifndef IPPROTO_UDP
#define IPPROTO_UDP     17
#endif


/*
 * Error codes
 */
#ifndef SOCK_OK
#define SOCK_OK         0
#endif
#ifndef ENOMEM
#define ENOMEM          12
#endif

#ifndef EINVAL
#define EINVAL          22
#endif

#ifndef ENOTSUP
#define ENOTSUP         134
#endif

#ifndef htons
#define htons(a)        NET_HTONS(a)
#endif


/* Additional definitions for SocketInterfaceExtended_t */

typedef NetworkContext_t * ( * TransportAllocate_t )( int32_t family,
                                                      int32_t type,
                                                      int32_t protocol );

typedef int32_t ( * TransportConnect_t )( NetworkContext_t * pxNetworkContext,
                                          const struct sockaddr * pxSocketAddress );

typedef int32_t ( * TransportConnectName_t )( NetworkContext_t * pNetworkContext,
                                             const char * pHostName,
                                             uint16_t port );

typedef int32_t ( * TransportDeallocate_t )( NetworkContext_t * pxNetworkContext );

typedef int32_t ( * TransportSetSockOpt_t )( NetworkContext_t * pxNetworkContext,
                                             int32_t option_name,
                                             const void *option_value,
                                             uint32_t option_len);


typedef struct TransportInterfaceExtended
{
    TransportRecv_t         recv;
    TransportSend_t         send;
    TransportAllocate_t     socket;
    TransportSetSockOpt_t   setsockopt;
    TransportConnect_t      connect;
    TransportConnectName_t  connect_name;
    TransportDeallocate_t   close;
} TransportInterfaceExtended_t;

#endif /* SOCKET_INTERFACE */
