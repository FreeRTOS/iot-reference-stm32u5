#ifndef _GREENGRASS_DISCOVERY_TASK_H_
#define _GREENGRASS_DISCOVERY_TASK_H_

#include "FreeRTOS.h"
#include <stdbool.h>

typedef struct
{
    bool ggDiscoverySuccess;        /* Boolean flag indicating if the GG Discovery was successful */
} GGDiscoveryContext;

void vGGDiscoveryTask( void * pvParameters );


#endif /* ifndef _GREENGRASS_DISCOVERY_TASK_H_ */
