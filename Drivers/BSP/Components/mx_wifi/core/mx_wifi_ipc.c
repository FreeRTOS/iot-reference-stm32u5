/**
  ******************************************************************************
  * @file    mx_wifi_ipc.c
  * @author  MCD Application Team
  * @brief   Host driver IPC protocol of MXCHIP Wi-Fi component.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */


/* Includes ------------------------------------------------------------------*/

#include "logging_levels.h"

#define LOG_LEVEL LOG_DEBUG

#include "logging.h"


/*cstat -MISRAC2012-* */
#include <stdint.h>
#include <stdio.h>
/*cstat +MISRAC2012-* */

#include <inttypes.h>

#include "mx_wifi.h"
#include "mx_wifi_ipc.h"
#include "mx_wifi_hci.h"


#define DEBUG_ERROR(M, ...)  LogError((M), ##__VA_ARGS__)

//#define MX_WIFI_IPC_DEBUG
//#ifdef MX_WIFI_IPC_DEBUG
#define DEBUG_LOG(M, ...)  LogDebug((M), ##__VA_ARGS__)
//#else
//#define DEBUG_LOG(M, ...)
//#endif /* MX_WIFI_IPC_DEBUG */

#define MIPC_REQ_LIST_SIZE      (64)

#define STRINGIFY(s) #s

const char * ipcIdToString( uint32_t req_id )
{
    const char * strVal;
    switch( req_id )
    {
    case MIPC_API_SYS_ECHO_CMD:
        strVal = "SYS_ECHO";
        break;
    case MIPC_API_SYS_REBOOT_CMD:
        strVal = "SYS_REBOOT";
        break;
    case MIPC_API_SYS_VERSION_CMD:
        strVal = "SYS_VERSION";
        break;
    case MIPC_API_SYS_RESET_CMD:
        strVal = "SYS_RESET";
        break;
    case MIPC_API_WIFI_GET_MAC_CMD:
        strVal = "WIFI_GET_MAC";
        break;
    case MIPC_API_WIFI_SCAN_CMD:
        strVal = "WIFI_SCAN";
        break;
    case MIPC_API_WIFI_CONNECT_CMD:
        strVal = "WIFI_CONNECT";
        break;
    case MIPC_API_WIFI_DISCONNECT_CMD:
        strVal = "WIFI_DISCONNECT";
        break;
    case MIPC_API_WIFI_SOFTAP_START_CMD:
        strVal = "WIFI_SOFTAP_START";
        break;
    case MIPC_API_WIFI_SOFTAP_STOP_CMD:
        strVal = "WIFI_SOFTAP_STOP";
        break;
    case MIPC_API_WIFI_GET_IP_CMD:
        strVal = "WIFI_GET_IP";
        break;
    case MIPC_API_WIFI_GET_LINKINFO_CMD:
        strVal = "WIFI_GET_LINKINFO";
        break;
    case MIPC_API_WIFI_PS_ON_CMD:
        strVal = "WIFI_PS_ON";
        break;
    case MIPC_API_WIFI_PS_OFF_CMD:
        strVal = "WIFI_PS_OFF";
        break;
    case MIPC_API_WIFI_PING_CMD:
        strVal = "WIFI_PING";
        break;
    case MIPC_API_WIFI_BYPASS_SET_CMD:
        strVal = "WIFI_BYPASS_SET";
        break;
    case MIPC_API_WIFI_BYPASS_GET_CMD:
        strVal = "WIFI_BYPASS_GET";
        break;
    case MIPC_API_WIFI_BYPASS_OUT_CMD:
        strVal = "WIFI_BYPASS_OUT";
        break;
    case MIPC_API_SOCKET_CREATE_CMD:
        strVal = "SOCKET_CREATE";
        break;
    case MIPC_API_SOCKET_CONNECT_CMD:
        strVal = "SOCKET_CONNECT";
        break;
    case MIPC_API_SOCKET_SEND_CMD:
        strVal = "SOCKET_SEND";
        break;
    case MIPC_API_SOCKET_SENDTO_CMD:
        strVal = "SOCKET_SENDTO";
        break;
    case MIPC_API_SOCKET_RECV_CMD:
        strVal = "SOCKET_RECV";
        break;
    case MIPC_API_SOCKET_RECVFROM_CMD:
        strVal = "SOCKET_RECVFROM";
        break;
    case MIPC_API_SOCKET_SHUTDOWN_CMD:
        strVal = "SOCKET_SHUTDOWN";
        break;
    case MIPC_API_SOCKET_CLOSE_CMD:
        strVal = "SOCKET_CLOSE";
        break;
    case MIPC_API_SOCKET_GETSOCKOPT_CMD:
        strVal = "SOCKET_GETSOCKOPT";
        break;
    case MIPC_API_SOCKET_SETSOCKOPT_CMD:
        strVal = "SOCKET_SETSOCKOPT";
        break;
    case MIPC_API_SOCKET_BIND_CMD:
        strVal = "SOCKET_BIND";
        break;
    case MIPC_API_SOCKET_LISTEN_CMD:
        strVal = "SOCKET_LISTEN";
        break;
    case MIPC_API_SOCKET_ACCEPT_CMD:
        strVal = "SOCKET_ACCEPT";
        break;
    case MIPC_API_SOCKET_SELECT_CMD:
        strVal = "SOCKET_SELECT";
        break;
    case MIPC_API_SOCKET_GETSOCKNAME_CMD:
        strVal = "SOCKET_GETSOCKNAME";
        break;
    case MIPC_API_SOCKET_GETPEERNAME_CMD:
        strVal = "SOCKET_GETPEERNAME";
        break;
    case MIPC_API_SOCKET_GETHOSTBYNAME_CMD:
        strVal = "SOCKET_GETHOSTBYNAME";
        break;
    case MIPC_API_TLS_SET_CLIENT_CERT_CMD:
        strVal = "TLS_SET_CLIENT_CERT";
        break;
    case MIPC_API_TLS_SET_SERVER_CERT_CMD:
        strVal = "TLS_SET_SERVER_CERT";
        break;
    case MIPC_API_TLS_ACCEPT_CMD:
        strVal = "TLS_ACCEPT";
        break;
    case MIPC_API_TLS_CONNECT_SNI_CMD:
        strVal = "TLS_CONNECT_SNI";
        break;
    case MIPC_API_TLS_SEND_CMD:
        strVal = "TLS_SEND";
        break;
    case MIPC_API_TLS_RECV_CMD:
        strVal = "TLS_RECV";
        break;
    case MIPC_API_TLS_CLOSE_CMD:
        strVal = "TLS_CLOSE";
        break;
    case MIPC_API_TLS_SET_NONBLOCK_CMD:
        strVal = "TLS_SET_NONBLOCK";
        break;
    case MIPC_API_MDNS_START_CMD:
        strVal = "MDNS_START";
        break;
    case MIPC_API_MDNS_STOP_CMD:
        strVal = "MDNS_STOP";
        break;
    case MIPC_API_MDNS_ANNOUNCE_CMD:
        strVal = "MDNS_ANNOUNCE";
        break;
    case MIPC_API_MDNS_DEANNOUNCE_CMD:
        strVal = "MDNS_DEANNOUNCE";
        break;
    case MIPC_API_MDNS_DEANNOUNCE_ALL_CMD:
        strVal = "MDNS_DEANNOUNCE_ALL";
        break;
    case MIPC_API_MDNS_IFACE_STATE_CHANGE_CMD:
        strVal = "MDNS_IFACE_STATE_CHANGE";
        break;
    case MIPC_API_MDNS_SET_HOSTNAME_CMD:
        strVal = "MDNS_SET_HOSTNAME";
        break;
    case MIPC_API_MDNS_SET_TXT_REC_CMD:
        strVal = "MDNS_SET_TXT_REC";
        break;
    case MIPC_API_SYS_REBOOT_EVENT:
        strVal = "SYS_REBOOT_EVENT";
        break;
    case MIPC_API_WIFI_STATUS_EVENT:
        strVal = "WIFI_STATUS_EVENT";
        break;
    case MIPC_API_WIFI_BYPASS_INPUT_EVENT:
        strVal = "WIFI_BYPASS_INPUT_EVENT";
    default:
        strVal = "";
    }
    return strVal;
}

/**
  * @brief IPC API event handlers
  */
typedef void (*event_callback_t)(mx_buf_t *mx_buff);
typedef struct _event_item_s
{
  uint16_t api_id;
  event_callback_t callback;
} event_item_t;

event_item_t event_table[3] =
{
  /* system */
  {MIPC_API_SYS_REBOOT_EVENT,         mapi_reboot_event_callback},
  /* wifi */
  {MIPC_API_WIFI_STATUS_EVENT,        mapi_wifi_status_event_callback},
  {MIPC_API_WIFI_BYPASS_INPUT_EVENT,  mapi_wifi_netlink_input_callback},
};

/**
  * @brief IPC API request list
  */
typedef struct _mipc_req_s
{
  uint32_t req_id;
  SEM_DECLARE(resp_flag);
  uint16_t *rbuffer_size; /* in/out*/
  uint8_t *rbuffer;
} mipc_req_t;


static mipc_req_t pending_request;

static uint32_t get_new_req_id(void);
static uint32_t mpic_get_req_id(uint8_t *buffer_in);
static uint16_t mpic_get_api_id(uint8_t *buffer_in);
static uint32_t mipc_event(mx_buf_t *netbuf);

/* unique sequence number */
static uint32_t get_new_req_id(void)
{
  static uint32_t id = 1;
  return id++;
}

static uint32_t mpic_get_req_id(uint8_t *buffer_in)
{
  return *((uint32_t *) & (buffer_in[MIPC_PKT_REQ_ID_OFFSET]));
}

static uint16_t mpic_get_api_id(uint8_t *buffer_in)
{
  return *((uint16_t *) & (buffer_in[MIPC_PKT_API_ID_OFFSET]));
}

static uint32_t mipc_event(mx_buf_t *netbuf)
{
  uint32_t req_id = MIPC_REQ_ID_NONE;
  uint16_t api_id = MIPC_API_ID_NONE;
  uint32_t i;
  event_callback_t callback;

  if (NULL != netbuf)
  {
    uint8_t *buffer_in = MX_NET_BUFFER_PAYLOAD(netbuf);
    uint32_t buffer_size = MX_NET_BUFFER_GET_PAYLOAD_SIZE(netbuf);

    if ((NULL != buffer_in) && (buffer_size >= MIPC_PKT_MIN_SIZE))
    {
      req_id = mpic_get_req_id(buffer_in);
      api_id = mpic_get_api_id(buffer_in);
      DEBUG_LOG("req_id: 0x%08"PRIx32", api_id: %s", req_id, ipcIdToString( api_id ) );
      if ((0 == (api_id & MIPC_API_EVENT_BASE)) && (MIPC_REQ_ID_NONE != req_id))
      {
        /* cmd response must match pending req id */
//        if(pending_request.req_id == req_id)
        if(1)
        {
          /* return params */
          if ((pending_request.rbuffer_size != NULL) && (*(pending_request.rbuffer_size) > 0)
              && (NULL != pending_request.rbuffer))
          {
            *(pending_request.rbuffer_size) = *(pending_request.rbuffer_size) < (buffer_size - MIPC_PKT_MIN_SIZE) ? \
                                              *(pending_request.rbuffer_size) : (buffer_size - MIPC_PKT_MIN_SIZE);
            memcpy(pending_request.rbuffer, buffer_in + MIPC_PKT_PARAMS_OFFSET, *(pending_request.rbuffer_size));
          }
          LogDebug("Signal for %d",pending_request.req_id);
          pending_request.req_id = 0xFFFFFFFF;
          if (SEM_OK != SEM_SIGNAL(pending_request.resp_flag))
          {
              LogDebug("Failed to signal command response. Dropping message");
              mx_wifi_hci_free(netbuf);
//            while (1);
          }
          else
          {
              mx_wifi_hci_free(netbuf);
          }
        }
        else
        {
            LogDebug("Dropping unexpected response message with req_id: %d", req_id);
            mx_wifi_hci_free(netbuf);
        }
      }
      else /* event callback */
      {
        for (i = 0; i < sizeof(event_table) / sizeof(event_item_t); i++)
        {
          if (event_table[i].api_id == api_id)
          {
            DEBUG_LOG("Got event: %s", ipcIdToString(api_id));
            callback = event_table[i].callback;
            if (NULL != callback)
            {
              DEBUG_LOG("callback with %p", buffer_in);
              callback(netbuf);
              break;
            }
          }
        }
        if (i == sizeof(event_table) / sizeof(event_item_t))
        {
          DEBUG_ERROR("unknown event: 0x%04x !", api_id);
          mx_wifi_hci_free(netbuf);
        }
      }
    }
    else
    {
      DEBUG_LOG("unknown buffer content");
      mx_wifi_hci_free(netbuf);
    }
  }
  return req_id;
}

/*******************************************************************************
  * IPC API implementations for mx_wifi over HCI
  ******************************************************************************/

/**
  * @brief                   mxchip ipc protocol init
  * @param  ipc_send         low level send function
  * @return int32_t          status code
  */
int32_t mipc_init(mipc_send_func_t ipc_send)
{
  int32_t ret;

  pending_request.req_id = 0xFFFFFFFF;
  SEM_INIT(pending_request.resp_flag, 1);

  ret = mx_wifi_hci_init(ipc_send);

  return ret;
}


int32_t mipc_deinit(void)
{
  int32_t ret;
  SEM_DEINIT(pending_request.resp_flag);
  ret = mx_wifi_hci_deinit();

  return ret;
}


int32_t mipc_request(uint16_t api_id, uint8_t *cparams, uint16_t cparams_size,
                     uint8_t *rbuffer, uint16_t *rbuffer_size, uint32_t timeout_ms)
{
  int32_t ret = MIPC_CODE_ERROR;

  uint8_t *cbuf;
  uint8_t *pos;
  uint16_t cbuf_size;
  uint32_t req_id;
  bool copy_buffer = true;

  LOCK(wifi_obj_get()->lockcmd);
  if (cparams_size <= MX_WIFI_IPC_PAYLOAD_SIZE)
  {
    /* create cmd data */
    cbuf_size = sizeof(req_id) + sizeof(api_id) + cparams_size;

#if MX_WIFI_TX_BUFFER_NO_COPY
    if (api_id == MIPC_API_WIFI_BYPASS_OUT_CMD)
    {
      cbuf = cparams - sizeof(req_id) - sizeof(api_id);
      copy_buffer = false;
    }
    else
#endif /* MX_WIFI_TX_BUFFER_NO_COPY */
    {
      cbuf = (uint8_t *)MX_WIFI_MALLOC(cbuf_size);
    }

    if (NULL != cbuf)
    {
      /* get an unique identifier */
      req_id = get_new_req_id();
      /* copy the protocol parameter to the head part of the buffer */
      pos = cbuf;
      memcpy(pos, &req_id, sizeof(req_id));
      pos += sizeof(req_id);
      memcpy(pos, &api_id, sizeof(api_id));
      pos += sizeof(api_id);

      if ((true == copy_buffer) && (cparams_size > 0))
      {
        memcpy(pos, cparams, cparams_size);
      }

      /* a single pending request due to LOCK usage on command */
      if (pending_request.req_id != 0xFFFFFFFF)
      {
        DEBUG_LOG("Error req_id must be 0xffffffff here %"PRIu32"", pending_request.req_id);
//        while (1);
      }
      pending_request.req_id = req_id;
      pending_request.rbuffer = rbuffer;
      pending_request.rbuffer_size = rbuffer_size;
      /* static int iter=0;                       */
      /* printf("%d push %d",iter++,cbuf_size); */

      /* send the command */
      DEBUG_LOG("Sending cmd req_id %"PRIu32", api_id: %s", req_id, ipcIdToString( api_id ) );
      ret = mx_wifi_hci_send(cbuf, cbuf_size);
      if (ret == 0)
      {
        /* wait for command answer */
        if (SEM_WAIT(pending_request.resp_flag, timeout_ms, mipc_poll) != SEM_OK)
        {
          DEBUG_ERROR("Error: command %s timeout(%"PRIu32" ms) waiting answer %"PRIu32"",
                  ipcIdToString( api_id ), timeout_ms, pending_request.req_id);
          pending_request.req_id = 0xFFFFFFFF;
          ret = MIPC_CODE_ERROR;
          MX_WIFI_FREE(cbuf);
          cbuf = NULL;
        }
      }
      else
      {
        DEBUG_ERROR("Failed to send command to Hci");
//        while (1);
        MX_WIFI_FREE(cbuf);
        cbuf = NULL;
      }
      DEBUG_LOG("done %"PRIu32"", req_id);
      if (true == copy_buffer)
      {
        MX_WIFI_FREE(cbuf);
      }
    }
  }
  UNLOCK(wifi_obj_get()->lockcmd);

  return ret;
}


/**
  * @brief                   mipc poll
  * @param  timeout_ms       timeout in ms
  * @return int32_t          0 if success, otherwise failed
  */
void mipc_poll(uint32_t timeout)
{
  mx_buf_t *nbuf;

  /* process the received data inside RX buffer */
  nbuf = mx_wifi_hci_recv(timeout);

  if (NULL != nbuf)
  {
    uint32_t len = MX_NET_BUFFER_GET_PAYLOAD_SIZE(nbuf);
    DEBUG_LOG("hci recv len %"PRIu32"", len);
    if (len > 0)
    {
      mipc_event(nbuf);
    }
    else
    {
      MX_NET_BUFFER_FREE(nbuf);
    }
  }
}


int32_t mipc_echo(uint8_t *in, uint16_t in_len, uint8_t *out, uint16_t *out_len,
                  uint32_t timeout)
{
  int32_t ret = MIPC_CODE_ERROR;

  if ((NULL != in) && (NULL != out) && (NULL != out_len))
  {
    ret = mipc_request(MIPC_API_SYS_ECHO_CMD, in, in_len, out, out_len, timeout);
    if (MIPC_CODE_SUCCESS == ret)
    {
      ret = MIPC_CODE_SUCCESS;
    }
    else
    {
      *out_len = 0;
    }
  }
  return ret;
}

/*******************************************************************************
  * IPC API event callbacks
  ******************************************************************************/

/* system */

void mapi_reboot_event_callback(mx_buf_t *buff)
{
  if (buff != NULL)
  {
    mx_wifi_hci_free(buff);
  }
  DEBUG_LOG("EVENT: reboot done.");
}

/* wifi */

void mapi_wifi_status_event_callback(mx_buf_t *nbuf)
{
  uint8_t cate;
  mwifi_status_t status;
  mx_wifi_status_callback_t status_cb = NULL;
  void *cb_args = NULL;

  if (NULL != nbuf)
  {
    uint8_t *payload = MX_NET_BUFFER_PAYLOAD(nbuf);
    status = *((mwifi_status_t *)(payload + MIPC_PKT_PARAMS_OFFSET));
    DEBUG_LOG("EVENT: wifi status: %02x", status);
    mx_wifi_hci_free(nbuf);

    switch (status)
    {
      case MWIFI_EVENT_STA_UP:
      case MWIFI_EVENT_STA_DOWN:
      case MWIFI_EVENT_STA_GOT_IP:
        cate = MC_STATION;
        status_cb = wifi_obj_get()->Runtime.status_cb[0];
        cb_args = wifi_obj_get()->Runtime.callback_arg[0];
        break;

      case MWIFI_EVENT_AP_UP:
      case MWIFI_EVENT_AP_DOWN:
        cate = MC_SOFTAP;
        status_cb = wifi_obj_get()->Runtime.status_cb[0];
        cb_args = wifi_obj_get()->Runtime.callback_arg[0];
        break;

      default:
        cate = MC_SOFTAP;
        MX_ASSERT(false);
        break;
    }

    if (NULL != status_cb)
    {
      status_cb(cate, status, cb_args);
    }
  }
}

void mapi_wifi_netlink_input_callback(mx_buf_t *nbuf)
{
  wifi_bypass_in_rparams_t *in_rprarams;
  /* DEBUG_LOG("IP stack in %d",len); */
  if (NULL != nbuf)
  {
    uint8_t     *buffer_in = MX_NET_BUFFER_PAYLOAD(nbuf);
    MX_STAT(callback);

    in_rprarams = (wifi_bypass_in_rparams_t *)(buffer_in + MIPC_PKT_PARAMS_OFFSET);
    MX_NET_BUFFER_HIDE_HEADER(nbuf, MIPC_PKT_PARAMS_OFFSET + sizeof(wifi_bypass_in_rparams_t));
    if ((NULL != wifi_obj_get()->Runtime.netlink_input_cb) && \
        (in_rprarams->tot_len > 0))
    {
      wifi_obj_get()->Runtime.netlink_input_cb(nbuf,
                                               wifi_obj_get()->Runtime.netlink_user_args);
    }
    else
    {
      MX_NET_BUFFER_FREE(nbuf);
    }
  }
}
