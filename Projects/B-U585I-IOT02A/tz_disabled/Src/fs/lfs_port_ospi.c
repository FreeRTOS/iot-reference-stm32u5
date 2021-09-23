/*
 * FreeRTOS STM32 Reference Integration
 *
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
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#include "logging_levels.h"
#define LOG_LEVEL LOG_DEBUG
#include "logging.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "lfs_util.h"
#include "lfs.h"
#include "lfs_port_prv.h"

#include "b_u585i_iot02a_ospi.h"
/*
 * LittleFS port for the external NOR flash connected to the STM32U5 octo-spi interface
 */

#ifdef LFS_NO_MALLOC
	static uint8_t __ALIGN_BEGIN ucReadBuffer[CONFIG_SIZE_CACHE_BUFFER] __ALIGN_END = { 0 };
	static uint8_t __ALIGN_BEGIN ucProgBuffer[CONFIG_SIZE_CACHE_BUFFER] __ALIGN_END = { 0 };
	static uint8_t __ALIGN_BEGIN ucLookAheadBuffer[CONFIG_SIZE_LOOKAHEAD_BUFFER] __ALIGN_END = { 0 };
	static struct lfs_config xLfsCfg = { 0 };
	static struct LfsPortCtx xLfsCtx = { 0 };
	static StaticSemaphore_t xMutexStatic;
#endif

/* Forward declarations */
static int lfs_port_read( const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void *buffer,
                          lfs_size_t size );

static int lfs_port_prog( const struct lfs_config *pxCfg,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void *buffer,
                          lfs_size_t size );

static int lfs_port_erase( const struct lfs_config *pxCfg,
                           lfs_block_t block );

static int lfs_port_sync( const struct lfs_config *c );


static void vPopulateConfig( struct lfs_config * pxCfg, struct LfsPortCtx * pxCtx )
{
    int32_t lError = BSP_ERROR_NONE;

    /* Fetch NOR flash info from BSP */
    BSP_OSPI_NOR_Info_t xNorInfo = { 0 };
    BSP_OSPI_NOR_Init_t xNorInit = { 0 };

    xNorInit.InterfaceMode = BSP_OSPI_NOR_OPI_MODE;
    xNorInit.TransferRate = MX25LM51245G_DTR_TRANSFER;

    lError = BSP_OSPI_NOR_Init( 0, &xNorInit );

    configASSERT( lError == BSP_ERROR_NONE );

    lError = BSP_OSPI_NOR_GetInfo( 0, &xNorInfo );

    configASSERT( lError == BSP_ERROR_NONE );

    /* Read size is one word */
    pxCfg->read_size = xNorInfo.ProgPageSize;
    pxCfg->prog_size = xNorInfo.ProgPageSize;

    /* Number of erasable blocks */
    pxCfg->block_count = xNorInfo.EraseSubSectorNumber;
    pxCfg->block_size = xNorInfo.EraseSubSectorSize;

    pxCfg->context = pxCtx;

    pxCfg->read = lfs_port_read;
    pxCfg->prog = lfs_port_prog;
    pxCfg->erase = lfs_port_erase;
    pxCfg->sync = lfs_port_sync;

#ifdef LFS_THREADSAFE
    pxCfg->lock = &lfs_port_lock;
    pxCfg->unlock = &lfs_port_unlock;
#endif
    /* controls wear leveling */
    pxCfg->block_cycles = 500;
    pxCfg->cache_size = xNorInfo.EraseSubSectorSize;
    pxCfg->lookahead_size = 256;

#ifdef LFS_NO_MALLOC
    pxCfg->read_buffer = ucReadBuffer;
    pxCfg->prog_buffer = ucProgBuffer;
    pxCfg->lookahead_buffer = ucLookAheadBuffer;
#else
    pxCfg->read_buffer = NULL;
    pxCfg->prog_buffer = NULL;
    pxCfg->lookahead_buffer = NULL;
#endif

    /* Accept default maximums for now */
    pxCfg->name_max = 0;
    pxCfg->file_max = 0;
    pxCfg->attr_max = 0;
    pxCfg->metadata_max = 0;
}

#ifndef LFS_THREADSAFE
#warning "Building littlefs with LFS_THREADSAFE is strongly suggested."
#endif

#ifdef LFS_NO_MALLOC
/*
 * Initializes littlefs on the internal storage of the STM32U5 without heap allocation.
 * @param xBlockTime Amount of time to wait for the flash interface lock
 */
const struct lfs_config * pxInitializeOSPIFlashFsStatic( TickType_t xBlockTime )
{
    xLfsCfg.context = ( void * ) &xLfsCtx;

    xLfsCtx.xMutex = xSemaphoreCreateMutexStatic(& ( &xMutexStatic ) );
    (void) xSemaphoreGive( xLfsCtx.xMutex );
    xLfsCtx.xBlockTime = xBlockTime;

    configASSERT( xLfsCtx.xMutex != NULL );

    vPopulateConfig( &xLfsCfg, &xLfsCtx );
}
#else
/*
 * Initializes littlefs on the internal storage of the STM32U5.
 * @param xBlockTime Amount of time to wait for the flash interface lock
 */
const struct lfs_config * pxInitializeOSPIFlashFs( TickType_t xBlockTime )
{
    /* Allocate space for lfs_config struct */
    struct lfs_config * pxCfg = ( struct lfs_config * ) pvPortMalloc( sizeof( struct lfs_config ) );
    configASSERT( pxCfg != NULL );

    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) ( pvPortMalloc( sizeof( struct LfsPortCtx ) ) );
    configASSERT( pxCtx != NULL );

    pxCtx->xBlockTime = xBlockTime;
    pxCtx->xMutex = xSemaphoreCreateMutex();

    configASSERT( pxCtx->xMutex != NULL );

    (void) xSemaphoreGive( pxCtx->xMutex );

    vPopulateConfig( pxCfg, pxCtx );

    return pxCfg;
}

#endif /* LFS_NO_MALLOC */

/*
 * Read bytes from the NOR flash device
 * @param c lfs_config structure for this block device
 * @param block Block number to read from
 * @param off Offset within block.
 * @param buffer Pointer to a buffer in which to store the resulting data
 * @param size Size of data to read and store in buffer
 */
static int lfs_port_read( const struct lfs_config *c,
                          lfs_block_t block,
        			      lfs_off_t off,
        			      void * buffer,
        			      lfs_size_t size )
{
    struct LfsPortCtx * pxCtx = (struct LfsPortCtx * ) c->context;

    configASSERT( c != NULL );
    configASSERT( block < c->block_count );
    configASSERT( buffer != NULL );
    configASSERT( size > 0 );
    configASSERT( xSemaphoreGetMutexHolder( pxCtx->xMutex ) == xTaskGetCurrentTaskHandle() );

    int32_t lReturnValue = BSP_ERROR_NONE;

    uint32_t ulReadAddr = ( block * c->block_size ) + off;

    /* Read while not in memory mapped mode */
    lReturnValue = BSP_OSPI_NOR_Read( 0, buffer, ulReadAddr, size );

    LogDebug( "Reading address %lu, size: %lu, rv: %ld", ulReadAddr, size, lReturnValue );

    configASSERT( lReturnValue == BSP_ERROR_NONE );

	return lReturnValue;
}


static int lfs_port_prog( const struct lfs_config *pxCfg,
                          lfs_block_t block,
        		     	  lfs_off_t off,
        		     	  const void *buffer,
        		     	  lfs_size_t size )
{
    /* validate arguments */
    configASSERT( pxCfg != NULL );
    configASSERT( block < pxCfg->block_count );
    configASSERT( ( off % pxCfg->prog_size ) == 0 );
    configASSERT( buffer != NULL );
    configASSERT( size > 0 );

    int32_t lReturnValue = BSP_ERROR_NONE;
    uint32_t ulWriteAddr = ( block * pxCfg->block_size ) + off;

	struct LfsPortCtx * pxCtx = (struct LfsPortCtx * ) pxCfg->context;

	configASSERT( xSemaphoreGetMutexHolder( pxCtx->xMutex ) == xTaskGetCurrentTaskHandle() );

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
	lReturnValue = BSP_OSPI_NOR_Write( 0, buffer, ulWriteAddr, size );
#pragma GCC diagnostic pop

	LogDebug( "Programming address %lu, size: %lu, rv: %ld", ulWriteAddr, size, lReturnValue );

	configASSERT( lReturnValue == BSP_ERROR_NONE );

	return lReturnValue;
}

static int lfs_port_erase( const struct lfs_config *pxCfg, lfs_block_t block )
{
    configASSERT( pxCfg != NULL );
    configASSERT( block < pxCfg->block_count );

	int32_t lReturnValue = BSP_ERROR_NONE;
	struct LfsPortCtx * pxCtx = (struct LfsPortCtx * ) pxCfg->context;

	configASSERT( xSemaphoreGetMutexHolder( pxCtx->xMutex ) == xTaskGetCurrentTaskHandle() );

	lReturnValue = BSP_OSPI_NOR_Erase_Block( 0, block * pxCfg->block_size, MX25LM51245G_ERASE_4K );

	LogDebug( "Erasing block %lu Return Value: %ld", block, lReturnValue );

	configASSERT( lReturnValue == BSP_ERROR_NONE );

	return lReturnValue;
}

static int lfs_port_sync( const struct lfs_config *c )
{
	return 0;
}
