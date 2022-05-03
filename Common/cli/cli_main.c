/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2017-2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 */

#include "FreeRTOS.h"
#include "task.h"

#include "cli.h"
#include "logging.h"
#include "cli_prv.h"
#include "stream_buffer.h"

#include "tls_transport_config.h"

#include <string.h>

typedef struct xCOMMAND_INPUT_LIST
{
    const CLI_Command_Definition_t * pxCommandLineDefinition;
    struct xCOMMAND_INPUT_LIST * pxNext;
} CLI_Definition_List_Item_t;


extern ConsoleIO_t xConsoleIO;
extern BaseType_t xInitConsoleUart( void );

char pcCliScratchBuffer[ CLI_OUTPUT_SCRATCH_BUF_LEN ];

/*
 * The callback function that is executed when "help" is entered.  This is the
 * only default command that is always present.
 */
static void prvHelpCommand( ConsoleIO_t * const pxConsoleIO,
                            uint32_t ulArgc,
                            char * ppcArgv[] );

/*
 * Return the number of arguments in the command string ( including the command name ).
 * This is used to build an posix-style "argc" variable.
 */
static uint32_t prvGetNumberOfArgs( const char * pcCommandString );

/* The definition of the "help" command.  This command is always at the front
 * of the list of registered commands. */
static const CLI_Command_Definition_t xHelpCommand =
{
    "help",
    "help:\r\n"
    "    List available commands and their arguments.\r\n"
    "    Usage:\r\n\n"
    "    help\r\n"
    "        Print help for all recognized commands\r\n\n"
    "    help <command>\r\n"
    "        Print help test for a specific command\r\n\n",
    prvHelpCommand
};

/* The definition of the list of commands.  Commands that are registered are
 * added to this list. */
static CLI_Definition_List_Item_t xRegisteredCommands =
{
    &xHelpCommand,
    NULL
};


/*-----------------------------------------------------------*/

BaseType_t FreeRTOS_CLIRegisterCommand( const CLI_Command_Definition_t * const pxCommandToRegister )
{
    static CLI_Definition_List_Item_t * pxLastCommandInList = &xRegisteredCommands;
    CLI_Definition_List_Item_t * pxNewListItem;
    BaseType_t xReturn = pdFAIL;

    /* Check the parameter is not NULL. */
    configASSERT( pxCommandToRegister );

    /* Create a new list item that will reference the command being registered. */
    pxNewListItem = ( CLI_Definition_List_Item_t * ) pvPortMalloc( sizeof( CLI_Definition_List_Item_t ) );
    configASSERT( pxNewListItem );

    if( pxNewListItem != NULL )
    {
        taskENTER_CRITICAL();
        {
            /* Reference the command being registered from the newly created
             * list item. */
            pxNewListItem->pxCommandLineDefinition = pxCommandToRegister;

            /* The new list item will get added to the end of the list, so
             * pxNext has nowhere to point. */
            pxNewListItem->pxNext = NULL;

            /* Add the newly created list item to the end of the already existing
             * list. */
            pxLastCommandInList->pxNext = pxNewListItem;

            /* Set the end of list marker to the new list item. */
            pxLastCommandInList = pxNewListItem;
        }
        taskEXIT_CRITICAL();

        xReturn = pdPASS;
    }

    return xReturn;
}
/*-----------------------------------------------------------*/


static const CLI_Definition_List_Item_t * prvFindMatchingCommand( const char * const pcCommandInput )
{
    const CLI_Definition_List_Item_t * pxCommand = &xRegisteredCommands;

    while( pxCommand != NULL )
    {
        const char * pcRegisteredCommandString = pxCommand->pxCommandLineDefinition->pcCommand;
        size_t xCommandStringLength = strlen( pcRegisteredCommandString );

        /* Match the provided command string to members of our list of commands */
        if( ( strncmp( pcCommandInput, pcRegisteredCommandString, xCommandStringLength ) == 0 ) &&
            ( ( pcCommandInput[ xCommandStringLength ] == ' ' ) || ( pcCommandInput[ xCommandStringLength ] == '\x00' ) ) )
        {
            break;
        }
        else
        {
            pxCommand = pxCommand->pxNext;
        }
    }

    return pxCommand;
}


/*-----------------------------------------------------------*/

void FreeRTOS_CLIProcessCommand( ConsoleIO_t * const pxCIO,
                                 char * pcCommandInput )
{
    const CLI_Definition_List_Item_t * pxCommand;

    /* Search for the command string in the list of registered commands. */
    pxCommand = prvFindMatchingCommand( pcCommandInput );

    if( pxCommand != NULL )
    {
        uint32_t ulArgC = prvGetNumberOfArgs( pcCommandInput );

        /* Tokenize into ulArgC / pcArgv */
        char * pcArgv[ ulArgC ]; /* TODO fix const */

        char * pcTokenizerCtx = NULL;

        /* pcArgv[ 0 ] is the command passed into the cli which was matched on */
        pcArgv[ 0 ] = strtok_r( pcCommandInput, " ", &pcTokenizerCtx );

        /* Subsequent members of pcArgv are command line parameters */
        for( uint32_t i = 1; i < ulArgC; i++ )
        {
            pcArgv[ i ] = strtok_r( NULL, " ", &pcTokenizerCtx );
            configASSERT( pcArgv[ i ] != NULL );
        }

        /* Assert that we read all of the tokens */
        configASSERT( strtok_r( NULL, " ", &pcTokenizerCtx ) == NULL );

        /* Call the callback function that is registered to this command. */
        pxCommand->pxCommandLineDefinition->pxCommandInterpreter( pxCIO, ulArgC, pcArgv );
    }
    else
    {
        /* pxCommand was NULL, the command was not found. */
        pxCIO->print( "Command not recognized. Enter 'help' to view a list of available commands.\r\n" );
    }
}

/*-----------------------------------------------------------*/

const char * FreeRTOS_CLIGetParameter( const char * pcCommandString,
                                       UBaseType_t uxWantedParameter,
                                       BaseType_t * pxParameterStringLength )
{
    UBaseType_t uxParametersFound = 0;
    const char * pcReturn = NULL;


    configASSERT( pxParameterStringLength );

    *pxParameterStringLength = 0;

    while( uxParametersFound < uxWantedParameter )
    {
        /* Index the character pointer past the current word.  If this is the start
         * of the command string then the first word is the command itself. */
        while( ( ( *pcCommandString ) != 0x00 ) && ( ( *pcCommandString ) != ' ' ) )
        {
            pcCommandString++;
        }

        /* Find the start of the next string. */
        while( ( ( *pcCommandString ) != 0x00 ) && ( ( *pcCommandString ) == ' ' ) )
        {
            pcCommandString++;
        }

        /* Was a string found? */
        if( *pcCommandString != 0x00 )
        {
            /* Is this the start of the required parameter? */
            uxParametersFound++;

            if( uxParametersFound == uxWantedParameter )
            {
                /* How long is the parameter? */
                pcReturn = pcCommandString;

                while( ( ( *pcCommandString ) != 0x00 ) && ( ( *pcCommandString ) != ' ' ) )
                {
                    ( *pxParameterStringLength )++;
                    pcCommandString++;
                }

                if( *pxParameterStringLength == 0 )
                {
                    pcReturn = NULL;
                }

                break;
            }
        }
        else
        {
            break;
        }
    }

    return pcReturn;
}

/*-----------------------------------------------------------*/

static void prvHelpCommand( ConsoleIO_t * const pxConsoleIO,
                            uint32_t ulArgc,
                            char * ppcArgv[] )
{
    static const CLI_Definition_List_Item_t * pxCommand = NULL;

    /* Check for an argument containing a recognized command */
    if( ( ulArgc > 1 ) &&
        ( ppcArgv[ 1 ] != NULL ) )
    {
        BaseType_t xFound = pdFALSE;
        pxCommand = &xRegisteredCommands;

        while( pxCommand != NULL &&
               xFound == pdFALSE )
        {
            if( strncmp( pxCommand->pxCommandLineDefinition->pcCommand,
                         ppcArgv[ 1 ],
                         strlen( pxCommand->pxCommandLineDefinition->pcCommand ) ) == 0 )
            {
                xFound = pdTRUE;
            }
            else
            {
                pxCommand = pxCommand->pxNext;
            }
        }
    }

    /* Print help for a single command if we found one specified */
    if( pxCommand != NULL )
    {
        pxConsoleIO->print( pxCommand->pxCommandLineDefinition->pcHelpString );
    }
    /* Otherwise, print help for all commands */
    else
    {
        pxCommand = &xRegisteredCommands;

        while( pxCommand != NULL )
        {
            pxConsoleIO->print( pxCommand->pxCommandLineDefinition->pcHelpString );
            pxCommand = pxCommand->pxNext;
        }
    }
}


/*-----------------------------------------------------------*/

static uint32_t prvGetNumberOfArgs( const char * pcCommandString )
{
    uint32_t luArgCount = 0;

    const char * pcCurrentChar = pcCommandString;

    /* Count the number of space delimited words in pcCommandString. */
    while( *pcCurrentChar != '\x00' )
    {
        /*
         * If the current character is not a space and
         * the next character is a space or null
         */
        if( ( pcCurrentChar[ 0 ] != ' ' ) &&
            ( ( pcCurrentChar[ 1 ] == ' ' ) ||
              ( pcCurrentChar[ 1 ] == '\x00' ) ) )
        {
            luArgCount++;
        }

        pcCurrentChar++;
    }

    return luArgCount;
}

void Task_CLI( void * pvParameters )
{
    ( void ) pvParameters;
    FreeRTOS_CLIRegisterCommand( &xCommandDef_conf );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_pki );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_ps );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_kill );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_killAll );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_heapStat );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_reset );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_uptime );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_rngtest );
    FreeRTOS_CLIRegisterCommand( &xCommandDef_assert );

    char * pcCommandBuffer = NULL;

    if( xInitConsoleUart() == pdTRUE )
    {
        for( ; ; )
        {
            /* Read a line of input */
            int32_t lLen = xConsoleIO.readline( &pcCommandBuffer );

            if( ( pcCommandBuffer != NULL ) &&
                ( lLen > 0 ) )
            {
                FreeRTOS_CLIProcessCommand( &xConsoleIO, pcCommandBuffer );
            }
        }
    }
    else
    {
        LogError( "Failed to initialize UART console." );
        vTaskDelete( NULL );
    }
}
