#ifndef _MQTT_AGENT_TASK_H_
#define _MQTT_AGENT_TASK_H_

#include "FreeRTOS.h"
#include <stdbool.h>
#include "kvstore_config.h"

struct MQTTAgentTaskCtx;
typedef struct MQTTAgentContext * MQTTAgentHandle_t;
typedef struct
{
    KVStoreKey_t endpointLabel;         /* Label for the MQTT endpoint */
    const char *caLabel;                /* Label for the Certificate Authority */
    KVStoreKey_t portLabel;             /* Label for the port used for the MQTT connection */
    uint32_t maxBackoffAttempts;        /* Maximum number of backoff attempts for reconnection */
    bool mqttAgentConnected;            /* Boolean flag indicating if the MQTT agent is connected */
} MQTTConnectionContext_t;

MQTTAgentHandle_t xGetMqttAgentHandle( void );

/* Event group based mechanism that can be used to block tasks until agent is ready */
void vSleepUntilMQTTAgentReady( void );

void vSleepUntilMQTTAgentConnected( void );

bool xIsMqttAgentConnected( void );

void vMQTTAgentTask( void * pvParameters );


#endif /* ifndef _MQTT_AGENT_TASK_H_ */
