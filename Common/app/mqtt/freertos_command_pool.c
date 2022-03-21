/*
 * FreeRTOS V202104.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

/**
 * @file freertos_command_pool.c
 * @brief Implements functions to obtain and release commands.
 */

#include "logging_levels.h"

#define LOG_LEVEL    LOG_ERROR

#include "logging.h"


/* Standard includes. */
#include <string.h>
#include <stdio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "semphr.h"

/* Header include. */
#include "freertos_command_pool.h"

/**
 * @brief The pool of command structures used to hold information on commands (such
 * as PUBLISH or SUBSCRIBE) between the command being created by an API call and
 * completion of the command by the execution of the command's callback.
 */
static MQTTAgentCommand_t commandStructurePool[ MQTT_COMMAND_CONTEXTS_POOL_SIZE ];

static QueueHandle_t xCommandPoolQueue = NULL;

/*-----------------------------------------------------------*/

void Agent_InitializePool( void )
{
    if( xCommandPoolQueue == NULL )
    {
        xCommandPoolQueue = xQueueCreate( MQTT_COMMAND_CONTEXTS_POOL_SIZE, sizeof( MQTTAgentCommand_t * ) );

        /* Populate the queue with pointers to each command structure. */
        for( uint32_t ulIdx = 0; ulIdx < MQTT_COMMAND_CONTEXTS_POOL_SIZE; ulIdx++ )
        {
            MQTTAgentCommand_t * pCommand = &commandStructurePool[ ulIdx ];

            ( void ) xQueueSend( xCommandPoolQueue, &pCommand, 0U );
        }
    }
}

/*-----------------------------------------------------------*/

MQTTAgentCommand_t * Agent_GetCommand( uint32_t ulBlockTimeMs )
{
    MQTTAgentCommand_t * pxCommandStruct = NULL;

    if( xCommandPoolQueue )
    {
        if( !xQueueReceive( xCommandPoolQueue, &pxCommandStruct, pdMS_TO_TICKS( ulBlockTimeMs ) ) )
        {
            LogError( ( "No command structure available." ) );
        }
    }
    else
    {
        LogError( ( "Command pool not initialized." ) );
    }

    return pxCommandStruct;
}

/*-----------------------------------------------------------*/

bool Agent_ReleaseCommand( MQTTAgentCommand_t * pCommandToRelease )
{
    BaseType_t xStructReturned = pdFALSE;

    if( !xCommandPoolQueue )
    {
        LogError( ( "Command pool not initialized." ) );
    }
    /* See if the structure being returned is actually from the pool. */
    else if( ( pCommandToRelease < commandStructurePool ) ||
             ( pCommandToRelease > ( commandStructurePool + MQTT_COMMAND_CONTEXTS_POOL_SIZE ) ) )
    {
        LogError( ( "Provided pointer: %p does not belong to the command pool.", pCommandToRelease ) );
    }
    else
    {
        xStructReturned = xQueueSend( xCommandPoolQueue, &pCommandToRelease, 0U );

        LogDebug( ( "Returned Command Context %d to pool",
                    ( int ) ( pCommandToRelease - commandStructurePool ) ) );
    }

    return ( bool ) xStructReturned;
}
