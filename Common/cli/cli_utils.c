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
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 */

/* Standard includes. */
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

#include "cli.h"
#include "cli_prv.h"

#include "core_cm33.h"

static void prvPSCommand( ConsoleIO_t * const pxConsoleIO,
                          uint32_t ulArgc,
                          char * ppcArgv[] );

static void vKillAllCommand( ConsoleIO_t * const pxCIO,
                             uint32_t ulArgc,
                             char * ppcArgv[] );

static void vKillCommand( ConsoleIO_t * const pxCIO,
                          uint32_t ulArgc,
                          char * ppcArgv[] );

static void vHeapStatCommand( ConsoleIO_t * const pxCIO,
                              uint32_t ulArgc,
                              char * ppcArgv[] );

static void vResetCommand( ConsoleIO_t * const pxCIO,
                           uint32_t ulArgc,
                           char * ppcArgv[] );

static void vUptimeCommand( ConsoleIO_t * const pxCIO,
                            uint32_t ulArgc,
                            char * ppcArgv[] );

static void vAssertCommand( ConsoleIO_t * const pxCIO,
                            uint32_t ulArgc,
                            char * ppcArgv[] );



const CLI_Command_Definition_t xCommandDef_ps =
{
    "ps",
    "ps\r\n"
    "    List the status of all running tasks and related runtime statistics.\r\n\n",
    prvPSCommand
};

const CLI_Command_Definition_t xCommandDef_kill =
{
    "kill",
    "kill\r\n"
    "    kill [ -SIGNAME ] <Task ID>\r\n"
    "        Signal a task with the named signal and the specified task id.\r\n\n"
    "    kill [ -n ] <Task ID>\r\n"
    "        Signal a task with the given signal number and the specified task id.\r\n\n",
    vKillCommand
};


const CLI_Command_Definition_t xCommandDef_killAll =
{
    "killall",
    "killall\r\n"
    "    killall [ -SIGNAME ] <Task Name>\r\n"
    "    killall [ -n ] <Task Name>\r\n"
    "        Signal a task with a given name with the signal number or signal name given.\r\n\n",
    vKillAllCommand
};

const CLI_Command_Definition_t xCommandDef_heapStat =
{
    "heapstat",
    "heapstat\r\n"
    "    heapstat [-b | --byte]\r\n"
    "        Display heap statistics in bytes.\r\n\n"
    "    heapstat -h | -k | --kibi\r\n"
    "        Display heap statistics in Kibibytes (KiB).\r\n\n"
    "    heapstat -m | --mebi\r\n"
    "        Display heap statistics in Mebibytes (MiB).\r\n\n"
    "    heapstat --kilo\r\n"
    "        Display heap statistics in Kilobytes (KB).\r\n\n"
    "    heapstat --mega\r\n"
    "        Display heap statistics in Megabytes (MB).\r\n\n",
    vHeapStatCommand
};

const CLI_Command_Definition_t xCommandDef_reset =
{
    "reset",
    "reset\r\n"
    "    Reset (reboot) the system.\r\n\n",
    vResetCommand
};

const CLI_Command_Definition_t xCommandDef_uptime =
{
    "uptime",
    "uptime\r\n"
    "    Display system uptime.\r\n\n",
    vUptimeCommand
};

const CLI_Command_Definition_t xCommandDef_assert =
{
    "assert",
    "assert\r\n"
    "   Cause a failed assertion.\r\n\n",
    vAssertCommand
};

/*-----------------------------------------------------------*/

/* Returns up to 9 character string representing the task state */
static inline const char * pceTaskStateToString( eTaskState xState )
{
    const char * pcState;

    switch( xState )
    {
        case eRunning:
            pcState = "RUNNING";
            break;

        case eReady:
            pcState = "READY";
            break;

        case eBlocked:
            pcState = "BLOCKED";
            break;

        case eSuspended:
            pcState = "SUSPENDED";
            break;

        case eDeleted:
            pcState = "DELETED";
            break;

        case eInvalid:
        default:
            pcState = "UNKNOWN";
            break;
    }

    return pcState;
}

/*-----------------------------------------------------------*/

static uint32_t ulGetStackDepth( TaskHandle_t xTask )
{
    struct tskTaskControlBlock
    {
        volatile StackType_t * pxDontCare0;

#if ( portUSING_MPU_WRAPPERS == 1 )
        xMPU_SETTINGS xDontCare1;
#endif
        ListItem_t xDontCare2;
        ListItem_t xDontCare3;
        UBaseType_t uxDontCare4;
        StackType_t * pxStack;
        char pcDontCare5[ configMAX_TASK_NAME_LEN ];

#if ( ( portSTACK_GROWTH > 0 ) || ( configRECORD_STACK_HIGH_ADDRESS == 1 ) )
        StackType_t * pxEndOfStack;
#endif
    };
    struct tskTaskControlBlock * pxTCB = ( struct tskTaskControlBlock * ) xTask;

    return( ( ( ( uintptr_t ) pxTCB->pxEndOfStack - ( uintptr_t ) pxTCB->pxStack ) / sizeof( StackType_t ) ) + 2 );
}

static void prvPSCommand( ConsoleIO_t * const pxCIO,
                          uint32_t ulArgc,
                          char * ppcArgv[] )
{
    UBaseType_t uxNumTasks = uxTaskGetNumberOfTasks();

    TaskStatus_t * pxTaskStatusArray = ( TaskStatus_t * ) pvPortMalloc( sizeof( TaskStatus_t ) * uxNumTasks );

    if( pxTaskStatusArray == NULL )
    {
        pxCIO->print( "Error: Not enough memory to complete the operation" );
    }
    else
    {
        unsigned long ulTotalRuntime = 0;
        uxNumTasks = uxTaskGetSystemState( pxTaskStatusArray,
                                           uxNumTasks,
                                           &ulTotalRuntime );

        ulTotalRuntime /= 100;

        snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, "Total Runtime: %lu\r\n", ulTotalRuntime );

        pxCIO->print( pcCliScratchBuffer );

        pxCIO->print( "+----------------------------------------------------------------------------------+\r\n" );
        pxCIO->print( "| Task |   State   |    Task Name     |___Priority__| %CPU | Stack | Stack | Stack |\r\n" );
        pxCIO->print( "|  ID  |           |                  | Base | Cur. |      | Alloc |  HWM  | Usage |\r\n" );
        pxCIO->print( "+----------------------------------------------------------------------------------+\r\n" );
        /* "| 1234 | AAAAAAAAA | AAAAAAAAAAAAAAAA |  00  |  00  | 000% | 00000 | 00000 | 000%  |" */

        for( uint32_t i = 0; i < uxNumTasks; i++ )
        {
            uint32_t ulStackSize = ulGetStackDepth( pxTaskStatusArray[ i ].xHandle );
            uint32_t ucStackUsagePct = ( 100 * ( ulStackSize - pxTaskStatusArray[ i ].usStackHighWaterMark ) / ulStackSize );
            snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                      "| %4lu | %-9s | %-16s |  %2lu  |  %2lu  | %3lu%% | %5lu | %5lu | %3lu%%  |\r\n",
                      pxTaskStatusArray[ i ].xTaskNumber,
                      pceTaskStateToString( pxTaskStatusArray[ i ].eCurrentState ),
                      pxTaskStatusArray[ i ].pcTaskName,
                      pxTaskStatusArray[ i ].uxBasePriority,
                      pxTaskStatusArray[ i ].uxCurrentPriority,
                      pxTaskStatusArray[ i ].ulRunTimeCounter / ulTotalRuntime,
                      ulStackSize,
                      ( uint32_t ) pxTaskStatusArray[ i ].usStackHighWaterMark,
                      ucStackUsagePct );

            pxCIO->print( pcCliScratchBuffer );
        }

        vPortFree( pxTaskStatusArray );
    }
}

typedef enum
{
    SIGHUP = 1,
    SIGINT = 2,
    SIGQUIT = 3,
    SIGKILL = 9,
    SIGTERM = 15,
    SIGSTOP = 23,
    SIGSTP = 24,
    SIGCONT = 25
} Signal_t;

struct
{
    Signal_t xSignal;
    const char * const pcSignalName;
}
pcSignaMap[] =
{
    { SIGHUP,  "SIGHUP"  },
    { SIGINT,  "SIGINT"  },
    { SIGQUIT, "SIGQUIT" },
    { SIGKILL, "SIGKILL" },
    { SIGTERM, "SIGTERM" },
    { SIGSTOP, "SIGSTOP" },
    { SIGSTP,  "SIGSTP"  },
    { SIGCONT, "SIGCONT" }
};

static void vSignalTask( TaskHandle_t xTask,
                         Signal_t xSignal )
{
    switch( xSignal )
    {
        case SIGQUIT:
        case SIGTERM:
            vTaskSuspend( xTask );
            vTaskDelete( xTask );
            break;

        case SIGSTOP:
        case SIGSTP:
            vTaskSuspend( xTask );
            break;

        case SIGCONT:
            vTaskResume( xTask );
            break;

        default:
            break;
    }
}

static TaskHandle_t xGetTaskHandleFromID( UBaseType_t uxTaskID )
{
    TaskHandle_t xTaskHandle = NULL;
    UBaseType_t uxNumTasks = uxTaskGetNumberOfTasks();

    TaskStatus_t * pxTaskStatusArray = ( TaskStatus_t * ) pvPortMalloc( sizeof( TaskStatus_t ) * uxNumTasks );

    if( pxTaskStatusArray != NULL )
    {
        unsigned long ulTotalRuntime = 0;
        uxNumTasks = uxTaskGetSystemState( pxTaskStatusArray,
                                           uxNumTasks,
                                           &ulTotalRuntime );

        for( uint32_t i = 0; i < uxNumTasks; i++ )
        {
            if( pxTaskStatusArray[ i ].xTaskNumber == uxTaskID )
            {
                xTaskHandle = pxTaskStatusArray[ i ].xHandle;
                break;
            }
        }

        vPortFree( pxTaskStatusArray );
    }

    return xTaskHandle;
}

static void vKillCommand( ConsoleIO_t * const pxCIO,
                          uint32_t ulArgc,
                          char * ppcArgv[] )
{
    Signal_t xTargetSignal = SIGTERM;
    UBaseType_t uxTaskId = 0;

    for( uint32_t i = 0; i < ulArgc; i++ )
    {
        /* Arg is a signal number or name */
        if( ppcArgv[ i ][ 0 ] == '-' )
        {
            char * pcArg = &( ppcArgv[ i ][ 1 ] );
            uint32_t ulSignal = strtoul( pcArg, NULL, 10 );

            if( ulSignal != 0 )
            {
                xTargetSignal = ulSignal;
            }
            else
            {
                for( uint32_t i = 0; i < ( sizeof( pcSignaMap ) / sizeof( pcSignaMap[ 0 ] ) ); i++ )
                {
                    if( strcmp( pcSignaMap[ i ].pcSignalName, pcArg ) == 0 )
                    {
                        xTargetSignal = pcSignaMap[ i ].xSignal;
                        break;
                    }
                }
            }
        }
        else /* Arg is a task number */
        {
            uxTaskId = strtoul( ppcArgv[ i ], NULL, 10 );
        }

        if( uxTaskId != 0 )
        {
            vSignalTask( xGetTaskHandleFromID( uxTaskId ), xTargetSignal );
        }
    }
}

static void vKillAllCommand( ConsoleIO_t * const pxCIO,
                             uint32_t ulArgc,
                             char * ppcArgv[] )
{
    Signal_t xTargetSignal = SIGTERM;
    TaskHandle_t xTaskHandle = NULL;

    for( uint32_t i = 0; i < ulArgc; i++ )
    {
        /* Arg is a signal number or name */
        if( ppcArgv[ i ][ 0 ] == '-' )
        {
            char * pcArg = &( ppcArgv[ i ][ 1 ] );
            uint32_t ulSignal = strtoul( pcArg, NULL, 10 );

            if( ulSignal != 0 )
            {
                xTargetSignal = ulSignal;
            }
            else
            {
                for( uint32_t i = 0; i < ( sizeof( pcSignaMap ) / sizeof( pcSignaMap[ 0 ] ) ); i++ )
                {
                    if( strcmp( pcSignaMap[ i ].pcSignalName, pcArg ) == 0 )
                    {
                        xTargetSignal = pcSignaMap[ i ].xSignal;
                        break;
                    }
                }
            }
        }
        else
        {
            xTaskHandle = xTaskGetHandle( ppcArgv[ i ] );
        }

        if( xTaskHandle != NULL )
        {
            vSignalTask( xTaskHandle, xTargetSignal );
        }
    }
}


/* only implemented for heap_4.c */
static void vHeapStatCommand( ConsoleIO_t * const pxCIO,
                              uint32_t ulArgc,
                              char * ppcArgv[] )
{
    size_t xDivisor = 1;
    const char * cDivSymbol = NULL;

    for( uint32_t i = 1; i < ulArgc; i++ )
    {
        if( ppcArgv[ i ][ 0 ] == '-' )
        {
            switch( ppcArgv[ i ][ 1 ] )
            {
                case '-':

                    if( strcmp( "--kilo", ppcArgv[ i ] ) == 0 )
                    {
                        xDivisor = 1000;
                    }
                    else if( strcmp( "--mega", ppcArgv[ i ] ) == 0 )
                    {
                        xDivisor = 1000 * 1000;
                    }
                    else if( strcmp( "--kibi", ppcArgv[ i ] ) == 0 )
                    {
                        xDivisor = 1024;
                    }
                    else if( strcmp( "--mebi", ppcArgv[ i ] ) == 0 )
                    {
                        xDivisor = 1024 * 1024;
                    }
                    else if( strcmp( "--byte", ppcArgv[ i ] ) == 0 )
                    {
                        xDivisor = 1;
                    }
                    else
                    {
                        pxCIO->print( "Error: Unrecognized argument: " );
                        pxCIO->print( ppcArgv[ i ] );
                        pxCIO->print( "\r\n" );
                        xDivisor = 0;
                    }

                    break;

                case 'k':
                case 'h':
                    xDivisor = 1024;
                    break;

                case 'm':
                    xDivisor = 1024 * 1024;
                    break;

                case 'b':
                    xDivisor = 1;
                    break;

                default:
                    pxCIO->print( "Error: Unrecognized argument: " );
                    pxCIO->print( ppcArgv[ i ] );
                    pxCIO->print( "\r\n" );
                    xDivisor = 0;
                    break;
            }
        }
        else
        {
            pxCIO->print( "Error: Unrecognized argument: " );
            pxCIO->print( ppcArgv[ i ] );
            pxCIO->print( "\r\n" );
        }
    }

    if( xDivisor != 0 )
    {
        switch( xDivisor )
        {
            case 1000:
                cDivSymbol = "(KB)   ";
                break;

            case 1024:
                cDivSymbol = "(KiB)  ";
                break;

            case ( 1000 * 1000 ):
                cDivSymbol = "(MB)   ";
                break;

            case ( 1024 * 1024 ):
                cDivSymbol = "(MiB)  ";
                break;

            case 1:
            default:
                cDivSymbol = "(Bytes)";
                break;
        }

        configASSERT( cDivSymbol != NULL );

        size_t xHeapSize = configTOTAL_HEAP_SIZE;
        size_t xHeapFree = xPortGetFreeHeapSize();
        size_t xMinHeapFree = xPortGetMinimumEverFreeHeapSize();
        size_t xHeapAlloc = xHeapSize - xHeapFree;
        size_t xMaxHeapAlloc = xHeapSize - xMinHeapFree;

        size_t xHeapSizeDiv = xHeapSize / xDivisor;
        size_t xHeapFreeDiv = xHeapFree / xDivisor;
        size_t xMinHeapFreeDiv = xMinHeapFree / xDivisor;
        size_t xHeapAllocDiv = xHeapAlloc / xDivisor;
        size_t xMaxHeapAllocDiv = xMaxHeapAlloc / xDivisor;

        size_t xHeapFreePct = ( 100 * xHeapFree ) / xHeapSize;
        size_t xMinHeapFreePct = ( 100 * xMinHeapFree ) / xHeapSize;
        size_t xHeapAllocPct = ( 100 * xHeapAlloc ) / xHeapSize;
        size_t xMaxHeapAllocPct = ( 100 * xMaxHeapAlloc ) / xHeapSize;

        size_t xLen = 0;

        static const char * pcFormatString = "| %-16s | %-11ld | 0x%-9X | %3lu %%   |\r\n";

        pxCIO->print( "+--------------------------------------------------------+\r\n" );

        xLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN,
                         "| Metric           | Dec %7s | Hex (Bytes) | %% Total |\r\n",
                         cDivSymbol );

        if( xLen >= CLI_OUTPUT_SCRATCH_BUF_LEN )
        {
            xLen = CLI_OUTPUT_SCRATCH_BUF_LEN - 1;
        }

        pxCIO->write( pcCliScratchBuffer, xLen );

        pxCIO->print( "|------------------|-------------|-------------|---------|\r\n" );



        xLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, pcFormatString,
                         "Heap Total", xHeapSizeDiv, xHeapSize, 100 );

        if( xLen >= CLI_OUTPUT_SCRATCH_BUF_LEN )
        {
            xLen = CLI_OUTPUT_SCRATCH_BUF_LEN - 1;
        }

        pxCIO->write( pcCliScratchBuffer, xLen );

        xLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, pcFormatString,
                         "Heap Free", xHeapFreeDiv, xHeapFree, xHeapFreePct );

        if( xLen >= CLI_OUTPUT_SCRATCH_BUF_LEN )
        {
            xLen = CLI_OUTPUT_SCRATCH_BUF_LEN - 1;
        }

        pxCIO->write( pcCliScratchBuffer, xLen );

        xLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, pcFormatString,
                         "Min. Heap Free", xMinHeapFreeDiv, xMinHeapFree, xMinHeapFreePct );

        if( xLen >= CLI_OUTPUT_SCRATCH_BUF_LEN )
        {
            xLen = CLI_OUTPUT_SCRATCH_BUF_LEN - 1;
        }

        pxCIO->write( pcCliScratchBuffer, xLen );

        xLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, pcFormatString,
                         "Heap Alloc.", xHeapAllocDiv, xHeapAlloc, xHeapAllocPct );

        if( xLen >= CLI_OUTPUT_SCRATCH_BUF_LEN )
        {
            xLen = CLI_OUTPUT_SCRATCH_BUF_LEN - 1;
        }

        pxCIO->write( pcCliScratchBuffer, xLen );

        xLen = snprintf( pcCliScratchBuffer, CLI_OUTPUT_SCRATCH_BUF_LEN, pcFormatString,
                         "Max. Heap Alloc.", xMaxHeapAllocDiv, xMaxHeapAlloc, xMaxHeapAllocPct );

        if( xLen >= CLI_OUTPUT_SCRATCH_BUF_LEN )
        {
            xLen = CLI_OUTPUT_SCRATCH_BUF_LEN - 1;
        }

        pxCIO->write( pcCliScratchBuffer, xLen );
        pxCIO->print( "+--------------------------------------------------------+\r\n" );
    }
}

static void vResetCommand( ConsoleIO_t * const pxCIO,
                           uint32_t ulArgc,
                           char * ppcArgv[] )
{
    pxCIO->print( "Resetting device." );
    vTaskDelay( pdMS_TO_TICKS( 100 ) );
    NVIC_SystemReset();
}

static void vUptimeCommand( ConsoleIO_t * const pxCIO,
                            uint32_t ulArgc,
                            char * ppcArgv[] )
{
    int lRslt = 0;

    const unsigned int MS_PER_SECOND = 1000;
    const unsigned int MS_PER_MINUTE = 60 * MS_PER_SECOND;
    const unsigned int MS_PER_HOUR = 60 * MS_PER_MINUTE;
    const unsigned int MS_PER_DAY = 24 * MS_PER_HOUR;

    unsigned long ulMsCount = ( xTaskGetTickCount() / portTICK_PERIOD_MS );

    ( void ) ulArgc;
    ( void ) ppcArgv;

    lRslt = snprintf( pcCliScratchBuffer,
                      CLI_OUTPUT_SCRATCH_BUF_LEN,
                      "up %lu day(s) %02lu:%02lu:%02lu.%03lu\r\n",
                      ulMsCount / MS_PER_DAY,
                      ( ulMsCount % MS_PER_DAY ) / MS_PER_HOUR,
                      ( ulMsCount % MS_PER_HOUR ) / MS_PER_MINUTE,
                      ( ulMsCount % MS_PER_MINUTE ) / MS_PER_SECOND,
                      ulMsCount % MS_PER_SECOND );

    if( ( lRslt > 0 ) &&
        ( lRslt < CLI_OUTPUT_SCRATCH_BUF_LEN ) )
    {
        pxCIO->write( pcCliScratchBuffer, ( size_t ) lRslt );
    }
}

static void vAssertCommand( ConsoleIO_t * const pxCIO,
                            uint32_t ulArgc,
                            char * ppcArgv[] )
{
    configASSERT( 0 );
}
