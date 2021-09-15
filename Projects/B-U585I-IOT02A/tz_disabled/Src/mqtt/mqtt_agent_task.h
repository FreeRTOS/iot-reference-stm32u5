#ifndef _MQTT_AGENT_TASK_H_
#define _MQTT_AGENT_TASK_H_

#include "FreeRTOS.h"

/* Event group based mechanism that can be used to block tasks until agent is ready */
void vSleepUntilMQTTAgentReady( void );

#endif
