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
#ifndef _MX_PRV_
#define _MX_PRV_

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C"
{
#endif
/* *INDENT-ON* */

/* Private definitions to be shared between mx driver files */
#include "message_buffer.h"
#include "iot_gpio_stm32_prv.h"
#include "task.h"
#include "mx_ipc.h"
#include "semphr.h"

#define LWIP_STACK

#ifdef LWIP_STACK
#include "mx_lwip.h"
#endif

#define DATA_WAITING_IDX                 3
#define DATA_WAITING_SNOTIFY             0x20
#define DATA_WAITING_CONTROL             0x10
#define DATA_WAITING_DATA                0x8

#define NET_EVT_IDX                      0x1
#define NET_LWIP_READY_BIT               0x1
#define NET_LWIP_IP_CHANGE_BIT           0x2
#define NET_LWIP_IFUP_BIT                0x4
#define NET_LWIP_IFDOWN_BIT              0x8
#define NET_LWIP_LINK_UP_BIT             0x10
#define NET_LWIP_LINK_DOWN_BIT           0x20
#define MX_STATUS_UPDATE_BIT             0x40
#define ASYNC_REQUEST_RECONNECT_BIT      0x80

/* Constants */
#define NUM_IPC_REQUEST_CTX              1
#define MX_DEFAULT_TIMEOUT_MS            100
#define MX_DEFAULT_TIMEOUT_TICK          pdMS_TO_TICKS( MX_DEFAULT_TIMEOUT_MS )
#define MX_TIMEOUT_CONNECT               pdMS_TO_TICKS( 120 * 1000 )
#define MX_MACADDR_LEN                   6
#define MX_FIRMWARE_REVISION_SIZE        24
#define MX_IP_LEN                        16
#define MX_SSID_BUF_LEN                  33
#define MX_PSK_BUF_LEN                   65
#define MX_BSSID_LEN                     MX_MACADDR_LEN
#define MX_SPI_TRANSACTION_TIMEOUT       MX_DEFAULT_TIMEOUT_TICK
#define MX_MAX_MESSAGE_LEN               4096
#define MX_ETH_PACKET_ENQUEUE_TIMEOUT    pdMS_TO_TICKS( 5 * 10000 )
#define MX_SPI_EVENT_TIMEOUT             pdMS_TO_TICKS( 10000 )
#define MX_SPI_FLOW_TIMEOUT              pdMS_TO_TICKS( 10 )

#define CONTROL_PLANE_QUEUE_LEN          10
#define DATA_PLANE_QUEUE_LEN             10
#define CONTROL_PLANE_BUFFER_SZ          ( 25 * sizeof( void * ) + sizeof( size_t ) )

typedef struct
{
    const IotMappedPin_t * gpio_flow;
    const IotMappedPin_t * gpio_reset;
    const IotMappedPin_t * gpio_nss;
    const IotMappedPin_t * gpio_notify;
    SPI_HandleTypeDef * pxSpiHandle;
    TaskHandle_t xDataPlaneTaskHandle;
    volatile uint32_t ulTxPacketsWaiting;
    volatile uint32_t ulLastRequestId;
    NetInterface_t * pxNetif;
    MessageBufferHandle_t xControlPlaneResponseBuff;
    QueueHandle_t xDataPlaneSendQueue;
    QueueHandle_t xControlPlaneSendQueue;
} MxDataplaneCtx_t;

typedef struct
{
    QueueHandle_t xControlPlaneSendQueue;
    MessageBufferHandle_t xControlPlaneResponseBuff; /* Message buffer for IPC message responses */
    TaskHandle_t xDataPlaneTaskHandle;
    MxEventCallback_t xEventCallback;
    void * pxEventCallbackCtx;
    volatile uint32_t * pulTxPacketsWaiting;
} ControlPlaneCtx_t;

typedef struct
{
    char pcFirmwareRevision[ MX_FIRMWARE_REVISION_SIZE + 1 ];
    MacAddress_t xMacAddress;
    NetInterface_t xNetif;
    volatile MxStatus_t xStatus;
    volatile MxStatus_t xStatusPrevious;
    QueueHandle_t xDataPlaneSendQueue;
    volatile uint32_t * pulTxPacketsWaiting;
    TaskHandle_t xNetTaskHandle;
    TaskHandle_t xDataPlaneTaskHandle;
} MxNetConnectCtx_t;

typedef enum
{
    /* Synchronous command / response IDs */
    IPC_SYS_OFFSET = 0,
    IPC_SYS_ECHO,   /* Not used by this implementation */
    IPC_SYS_REBOOT, /* Not used by this implementation */
    IPC_SYS_VERSION,
    IPC_SYS_RESET,
    IPC_WIFI_OFFSET = 0x100,
    IPC_WIFI_GET_MAC,
    IPC_WIFI_SCAN, /* Not used by this implementation */
    IPC_WIFI_CONNECT,
    IPC_WIFI_DISCONNECT,
    IPC_WIFI_SOFTAP_START, /* Not used by this implementation */
    IPC_WIFI_SOFTAP_STOP,  /* Not used by this implementation */
    IPC_WIFI_GET_IP,       /* Not used by this implementation */
    IPC_WIFI_GET_LINKINFO, /* Not used by this implementation */
    IPC_WIFI_PS_ON,        /* Not used by this implementation */
    IPC_WIFI_PS_OFF,       /* Not used by this implementation */
    IPC_WIFI_PING,         /* Not used by this implementation */
    IPC_WIFI_BYPASS_SET,
    IPC_WIFI_BYPASS_GET,
    IPC_WIFI_BYPASS_OUT,

    /* Asynchronous Events */
    IPC_SYS_EVT_OFFSET = 0x8000,
    IPC_SYS_EVT_REBOOT,
    IPC_WIFI_EVT_OFFSET = IPC_SYS_EVT_OFFSET + 0x100,
    IPC_WIFI_EVT_STATUS,
    IPC_WIFI_EVT_BYPASS_IN
} IPCCommand_t;


/* Request packet definitions (aligned on a byte boundary) */
#pragma pack(1)

/* IPC_SYS_VERSION */
typedef struct IPCResponseSysVersion
{
    char cFirmwareRevision[ MX_FIRMWARE_REVISION_SIZE ];
} IPCResponseSysVersion_t;


/* IPC_WIFI_GET_MAC */
typedef struct IPCResponseWifiGetMac_t
{
    uint8_t ucMacAddress[ MX_MACADDR_LEN ];
} IPCResponseWifiGetMac_t;


/* IPC_WIFI_CONNECT */
typedef struct IPInfoType
{
    char cLocalIP[ MX_IP_LEN ];
    char cNetmask[ MX_IP_LEN ];
    char cGateway[ MX_IP_LEN ];
    char cDnsServer[ MX_IP_LEN ];
} IPInfoType_t;

typedef enum
{
    WIFI_SEC_NONE = 0,
    WIFI_SEC_WEP,
    WIFI_WEC_WPA_TKIP,
    WIFI_SEC_WPA_AES,
    WIFI_SEC_WPA2_TKIP,
    WIFI_SEC_WPA2_AES,
    WIFI_SEC_WPA2_MIXED,
    WIFI_SEC_AUTO
} MxWifiSecurity_t;

typedef struct IPCRequestWifiConnect
{
    char cSSID[ MX_SSID_BUF_LEN ]; /* 32 character string + null terminator */
    char cPSK[ MX_PSK_BUF_LEN ];   /* 64 character string + null terminator */
    int32_t lKeyLength;
    uint8_t ucUseAttr;
    uint8_t ucUseStaticIp;
    uint8_t ucAccessPointBssid[ MX_BSSID_LEN ];
    uint8_t ucAccessPointChannel;
    uint8_t ucSecurityType;
    IPInfoType_t xStaticIpInfo;
} IPCRequestWifiConnect_t;


/* IPC_WIFI_DISCONNECT */
/* IPC_WIFI_EVT_STATUS */
struct IPCResponseStatus
{
    uint32_t status;
};

typedef struct IPCResponseStatus IPCResponseWifiDisconnect_t;

/* IPC_WIFI_BYPASS_SET */

typedef struct IPCRequestWifiBypassSet
{
    int32_t enable;
} IPCRequestWifiBypassSet_t;

typedef IPCRequestWifiBypassSet_t IPCRequestWifiBypassGet_t;

/* IPC_WIFI_BYPASS_OUT */
typedef struct IPCResponseStatus  IPCResponsBypassOut_t;

/* IPC_WIFI_EVT_STATUS */
typedef struct IPCResponseStatus  IPCEventStatus_t;

/* Union representing all possible packet data contents */
typedef union
{
    IPCResponseSysVersion_t xResponseSysVersion;
    IPCResponseWifiGetMac_t xResponseWifiGetMac;
    IPCRequestWifiConnect_t xRequestWifiConnect;
    IPCResponseWifiDisconnect_t xRequestWifiDisconnect;
    IPCRequestWifiBypassSet_t xRequestWifiBypassSet;
    IPCRequestWifiBypassGet_t xRequestWifiBypassGet;
    IPCEventStatus_t xEventStatus;
} IPCPacketData_t;

/* Struct representing packet header (without SPI-specific header) */
typedef struct
{
    uint32_t ulIPCRequestId;
    uint16_t usIPCApiId; /* IPCCommand_t */
} IPCHeader_t;

typedef struct
{
    IPCHeader_t xHeader;
    IPCPacketData_t xData;
} IPCPacket_t;

#define MX_BYPASS_PAD_LEN    16

/* IPC_WIFI_BYPASS_OUT */
/* IPC_WIFI_EVT_BYPASS_IN */
typedef struct
{
    IPCHeader_t xHeader;
    int32_t lIndex; /* WifiBypassMode_t */
    uint8_t ucPad[ MX_BYPASS_PAD_LEN ];
    uint16_t usDataLen;
} BypassInOut_t;

#pragma pack(1)
typedef struct
{
    uint8_t type;
    uint16_t len;
    uint16_t lenx;
    uint8_t pad[ 3 ];
} SPIHeader_t;
#pragma pack()

#define MX_MAX_MTU       1500
#define MX_RX_BUFF_SZ    ( MX_MAX_MTU + sizeof( BypassInOut_t ) + PBUF_LINK_HLEN )

/*
 * static inline void vPrintBuffer( const char * ucPrefix,
 *                               uint8_t * pcBuffer,
 *                               uint32_t usDataLen )
 * {
 *  char * ucPrintBuf = pvPortMalloc( 2 * usDataLen + 1 );
 *  if( ucPrintBuf != NULL )
 *  {
 *      for( uint32_t i = 0; i < usDataLen; i++ )
 *      {
 *          snprintf( &ucPrintBuf[ 2 * i ], 3, "%02X", pcBuffer[i] );
 *      }
 *      ucPrintBuf[ 2 * usDataLen ] = 0;
 *      LogError("%s: %s", ucPrefix, ucPrintBuf);
 *      vPortFree(ucPrintBuf);
 *  }
 * }
 *
 * static inline void vPrintSpiHeader( const char * ucPrefix,
 *                                  SPIHeader_t * pxHdr )
 * {
 *  LogError("%s: Type: 0x%02X, Len: 0x%04X, Lenx: 0x%04X", ucPrefix, pxHdr->type, pxHdr->len, pxHdr->lenx );
 * }
 *
 */


BaseType_t prvxLinkInput( NetInterface_t * pxNetif,
                          PacketBuffer_t * pxPbufIn );
void prvControlPlaneRouter( void * pvParameters );
uint32_t prvGetNextRequestID( void );
void vDataplaneThread( void * pvParameters );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* _MX_PRV_ */
