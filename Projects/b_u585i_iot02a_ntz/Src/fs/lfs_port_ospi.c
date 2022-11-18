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
#define LOG_LEVEL    LOG_ERROR
#include "logging.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "lfs_util.h"
#include "lfs.h"
#include "lfs_port_prv.h"
#include "ospi_nor_mx25lmxxx45g.h"

/*
 * LittleFS port for the external NOR flash connected to the STM32U5 octo-spi interface
 */

#ifdef LFS_NO_MALLOC
static uint8_t __ALIGN_BEGIN ucReadBuffer[ CONFIG_SIZE_CACHE_BUFFER ] __ALIGN_END = { 0 };
static uint8_t __ALIGN_BEGIN ucProgBuffer[ CONFIG_SIZE_CACHE_BUFFER ] __ALIGN_END = { 0 };
static uint8_t __ALIGN_BEGIN ucLookAheadBuffer[ CONFIG_SIZE_LOOKAHEAD_BUFFER ] __ALIGN_END = { 0 };
static struct lfs_config xLfsCfg = { 0 };
static struct LfsPortCtx xLfsCtx = { 0 };
static StaticSemaphore_t xMutexStatic;
#endif


/* Forward declarations */
static int lfs_port_read( const struct lfs_config * c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void * pvBuffer,
                          lfs_size_t size );

static int lfs_port_prog( const struct lfs_config * pxCfg,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void * pvBuffer,
                          lfs_size_t size );

static int lfs_port_erase( const struct lfs_config * pxCfg,
                           lfs_block_t block );

static int lfs_port_sync( const struct lfs_config * c );



static void vPopulateConfig( struct lfs_config * pxCfg,
                             struct LfsPortCtx * pxCtx )
{
    /* Read size is one word */
    pxCfg->read_size = 1;
    pxCfg->prog_size = 256;

    /* Number of erasable blocks */
    pxCfg->block_count = ( MX25LM_MEM_SZ_USABLE / MX25LM_SECTOR_SZ );
    pxCfg->block_size = MX25LM_SECTOR_SZ;

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
    pxCfg->cache_size = 4096;
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

    xLfsCtx.xMutex = xSemaphoreCreateMutexStatic( &( &xMutexStatic ) );
    ( void ) xSemaphoreGive( xLfsCtx.xMutex );
    xLfsCtx.xBlockTime = xBlockTime;

    configASSERT( xLfsCtx.xMutex != NULL );

    vPopulateConfig( &xLfsCfg, &xLfsCtx );
}
#else /* ifdef LFS_NO_MALLOC */

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


    vPopulateConfig( pxCfg, pxCtx );

    BaseType_t xSuccess = ospi_Init( &( pxCtx->xOSPIHandle ) );

    configASSERT( xSuccess == pdTRUE );

    ( void ) xSemaphoreGive( pxCtx->xMutex );

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
static int lfs_port_read( const struct lfs_config * c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void * pvBuffer,
                          lfs_size_t size )
{
    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) c->context;

    configASSERT( c != NULL );
    configASSERT( block < c->block_count );
    configASSERT( pvBuffer != NULL );
    configASSERT( size > 0 );

    int32_t lReturnValue = 0;

    uint32_t ulReadAddr = OPI_START_ADDRESS + ( block * c->block_size ) + off;

    if( ospi_ReadAddr( &( pxCtx->xOSPIHandle ),
                       ulReadAddr,
                       pvBuffer,
                       size,
                       pdMS_TO_TICKS( MX25LM_READ_TIMEOUT_MS ) ) != pdTRUE )
    {
        lReturnValue = -1;
    }

    LogDebug( "Reading address 0x%010lX, size: %lu, rv: %ld", ulReadAddr, size, lReturnValue );

    return lReturnValue;
}


static int lfs_port_prog( const struct lfs_config * pxCfg,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void * pvBuffer,
                          lfs_size_t size )
{
    /* validate arguments */
    configASSERT( pxCfg != NULL );
    configASSERT( block < pxCfg->block_count );
    configASSERT( pvBuffer != NULL );
    configASSERT( size > 0 );

    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) pxCfg->context;

    int32_t lReturnValue = 0;

    configASSERT( ( size % MX25LM_PROGRAM_FIFO_LEN ) == 0 );

    /* Determine the 4-byte write address */
    uint32_t ulStartAddr = OPI_START_ADDRESS + ( block * pxCfg->block_size ) + off;

    uint32_t ulLastAddr = ulStartAddr + size - MX25LM_PROGRAM_FIFO_LEN;

    LogDebug( "Programming Start Addr: 0x%010lX, End Addr: 0x%010lX, size: %lu, block: %lu, offset: %lu, rv: %ld",
              ulStartAddr, ulLastAddr, size, block, off, lReturnValue );

    for( uint32_t ulWriteAddr = ulStartAddr; ulWriteAddr <= ulLastAddr; ulWriteAddr += MX25LM_PROGRAM_FIFO_LEN )
    {
        LogDebug( "Writing block at addr: 0x%010lX, len: %lu", ulWriteAddr, MX25LM_PROGRAM_FIFO_LEN );

        if( ospi_WriteAddr( &( pxCtx->xOSPIHandle ),
                            ulWriteAddr,
                            &( ( ( uint8_t * ) pvBuffer )[ ulWriteAddr - ulStartAddr ] ),
                            MX25LM_PROGRAM_FIFO_LEN,
                            pdMS_TO_TICKS( MX25LM_WRITE_TIMEOUT_MS ) ) != pdTRUE )
        {
            lReturnValue = -1;
            break;
        }
    }

    return lReturnValue;
}

static int lfs_port_erase( const struct lfs_config * pxCfg,
                           lfs_block_t block )
{
    configASSERT( pxCfg != NULL );
    configASSERT( block < pxCfg->block_count );

    int32_t lReturnValue = 0;
    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) pxCfg->context;

    /* Determine the 4-byte erase address */
    uint32_t ulEraseAddr = OPI_START_ADDRESS + ( block * pxCfg->block_size );

    LogDebug( "Starting erase operation addr: 0x%010lX ", ulEraseAddr );

    if( ospi_EraseSector( &( pxCtx->xOSPIHandle ),
                          ulEraseAddr,
                          pdMS_TO_TICKS( MX25LM_ERASE_TIMEOUT_MS ) ) != pdTRUE )
    {
        lReturnValue = -1;
    }

    LogDebug( "Erase operation completed. Address: 0x%010lX Return Value: %ld", ulEraseAddr, lReturnValue );

    return lReturnValue;
}

static int lfs_port_sync( const struct lfs_config * c )
{
    return 0;
}
