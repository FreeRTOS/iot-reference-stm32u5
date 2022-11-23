#ifndef _MQTT_AGENT_TASK_H_
#define _MQTT_AGENT_TASK_H_

#include "FreeRTOS.h"
#include <stdbool.h>

struct MQTTAgentTaskCtx;
typedef struct MQTTAgentContext * MQTTAgentHandle_t;

MQTTAgentHandle_t xGetMqttAgentHandle( void );

/* Event group based mechanism that can be used to block tasks until agent is ready */
void vSleepUntilMQTTAgentReady( void );

void vSleepUntilMQTTAgentConnected( void );

bool xIsMqttAgentConnected( void );

void vMQTTAgentTask( void * pvParameters );


#endif /* ifndef _MQTT_AGENT_TASK_H_ */
