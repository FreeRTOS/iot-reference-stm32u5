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

#include "logging.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "lfs_util.h"
#include "lfs.h"
#include "lfs_port_prv.h"

#include "stm32u585xx.h"
#include "stm32u5xx.h"
#include "stm32u5xx_hal_flash.h"
#include "stm32u5xx_hal_flash_ex.h"

/* Uses all pages of Bank 2
 *
 */
/*TODO: Modify to use a single portion of the first flash bank and handle flash bank-swapping for OTA. */

/* Use second bank to avoid program flash. TODO: Should add variable in linker script to mark end of program flash */
#define CONFIG_LFS_FLASH_BASE        ( FLASH_BASE + FLASH_BANK_SIZE )
#define LFS_CONFIG_LOOKAHEAD_SIZE    16
#define LFS_CONFIG_CACHE_SIZE        16

#ifdef LFS_NO_MALLOC
static uint8_t __ALIGN_BEGIN ucReadBuffer[ CONFIG_SIZE_CACHE_BUFFER ] __ALIGN_END = { 0 };
static uint8_t __ALIGN_BEGIN ucProgBuffer[ CONFIG_SIZE_CACHE_BUFFER ] __ALIGN_END = { 0 };
static uint8_t __ALIGN_BEGIN ucLookAheadBuffer[ CONFIG_SIZE_LOOKAHEAD_BUFFER ] __ALIGN_END = { 0 };
static struct lfs_config xLfsCfg = { 0 };
static struct LfsPortCtx xLfsCtx = { 0 };
static StaticSemaphore_t xMutexStatic;
#endif

static int lfs_port_read( const struct lfs_config * c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void * buffer,
                          lfs_size_t size )
{
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG( FLASH_FLAG_ALL_ERRORS );

    uint32_t src_address = CONFIG_LFS_FLASH_BASE + block * c->block_size + off;

    ( void ) memcpy( buffer, ( void * ) src_address, size );

    HAL_FLASH_Lock();

    return 0;
}

static int lfs_port_prog( const struct lfs_config * c,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void * buffer,
                          lfs_size_t size )
{
    HAL_StatusTypeDef xHAL_Status = HAL_OK;
    uint32_t n_rows = size / c->prog_size;
    uint32_t dest_address = 0;
    uint32_t src_address = 0;
    uint32_t block_base_addr = CONFIG_LFS_FLASH_BASE + block * c->block_size;

    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) c->context;

    configASSERT( xQueueGetMutexHolder( pxCtx->xMutex ) == xTaskGetCurrentTaskHandle() );

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG( FLASH_FLAG_ALL_ERRORS );

    for( uint32_t i_row = 0; i_row < n_rows; i_row++ )
    {
        dest_address = block_base_addr + off + i_row * 4 * sizeof( uint32_t );
        src_address = ( uint32_t ) buffer + i_row * 4 * sizeof( uint32_t );
        xHAL_Status = HAL_FLASH_Program( FLASH_TYPEPROGRAM_QUADWORD, dest_address, src_address );

        if( xHAL_Status != HAL_OK )
        {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    HAL_FLASH_Lock();

    return 0;
}

static int lfs_port_erase( const struct lfs_config * c,
                           lfs_block_t block )
{
    uint32_t ulPageError = 0;
    FLASH_EraseInitTypeDef xErase_Config = { 0 };
    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) c->context;

    configASSERT( xQueueGetMutexHolder( pxCtx->xMutex ) == xTaskGetCurrentTaskHandle() );

    xErase_Config.TypeErase = FLASH_TYPEERASE_PAGES;
    xErase_Config.Banks = FLASH_BANK_2;
    xErase_Config.Page = block;
    xErase_Config.NbPages = 1;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG( FLASH_FLAG_ALL_ERRORS );
    HAL_StatusTypeDef xHAL_Status = HAL_FLASHEx_Erase( &xErase_Config, &ulPageError );

    HAL_FLASH_Lock();

    return xHAL_Status == HAL_OK ? 0 : -1;
}

static int lfs_port_sync( const struct lfs_config * c )
{
    return 0;
}

static void vPopulateConfig( struct lfs_config * pxCfg,
                             struct LfsPortCtx * pxCtx )
{
    /* Store the mutex handle as the context */
    pxCfg->context = pxCtx;

    pxCfg->read = lfs_port_read;
    pxCfg->prog = lfs_port_prog;
    pxCfg->erase = lfs_port_erase;
    pxCfg->sync = lfs_port_sync;

#ifdef LFS_THREADSAFE
    pxCfg->lock = &lfs_port_lock;
    pxCfg->lock = &lfs_port_unlock;
#endif

    pxCfg->read_size = 1;
    pxCfg->prog_size = 16;
    pxCfg->block_size = FLASH_PAGE_SIZE;

    pxCfg->block_count = FLASH_PAGE_NB;
    pxCfg->block_cycles = 500;

    pxCfg->cache_size = LFS_CONFIG_CACHE_SIZE;
    pxCfg->lookahead_size = LFS_CONFIG_LOOKAHEAD_SIZE;

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

#ifdef LFS_NO_MALLOC

/*
 * Initializes littlefs on the internal storage of the STM32U5 without heap allocation.
 * @param xBlockTime Amount of time to wait for the flash interface lock
 */
const struct lfs_config * pxInitializeInternalFlashFsStatic( TickType_t xBlockTime )
{
    xLfsCfg.context = ( void * ) &xLfsCtx;

    xLfsCtx.xMutex = xSemaphoreCreateMutexStatic( &( &xMutexStatic ) );
    xLfsCtx.xBlockTime = xBlockTime;

    configASSERT( xLfsCtx.xMutex != NULL );

    vPopulateConfig( &xLfsCfg, &xLfsCtx );
}
#else /* ifdef LFS_NO_MALLOC */

/*
 * Initializes littlefs on the internal storage of the STM32U5.
 * @param xBlockTime Amount of time to wait for the flash interface lock
 */
const struct lfs_config * pxInitializeInternalFlashFs( TickType_t xBlockTime )
{
    /* Allocate space for lfs_config struct */
    struct lfs_config * pxCfg = ( struct lfs_config * ) pvPortMalloc( sizeof( struct lfs_config ) );

    configASSERT( pxCfg != NULL );

    struct LfsPortCtx * pxCtx = ( struct LfsPortCtx * ) ( pvPortMalloc( sizeof( struct lfs_config ) ) );

    configASSERT( pxCtx != NULL );

    pxCtx->xBlockTime = xBlockTime;
    pxCtx->xMutex = xSemaphoreCreateMutex();
    configASSERT( pxCtx->xMutex != NULL );

    vPopulateConfig( pxCfg, pxCtx );
    return pxCfg;
}
#endif /* LFS_NO_MALLOC */

#if 0

int lfs_port_demo( void )
{
    lfs_t lfs = { 0 };
    lfs_file_t file = { 0 };
    const char * path = "boot_count";

#ifdef LFS_NO_MALLOC
    static uint8_t __ALIGN_BEGIN ucFileCache[ CONFIG_SIZE_CACHE_BUFFER ] __ALIGN_END = { 0 };
    struct lfs_file_config xFileConfig = { 0 };

    xFileConfig.buffer = ( void * ) ucFileCache;
#endif

    struct lfs_config * pCfg = lfs_port_get_config();

    /* mount the filesystem */
    int err = lfs_mount( &lfs, pCfg );

    /* reformat if we can't mount the filesystem */
    /* this should only happen on the first boot */
    if( err )
    {
        LogWarn( "Failed to mount partition. Formatting...\n" );
        lfs_format( &lfs, pCfg );
        err = lfs_mount( &lfs, pCfg );
    }

    if( err )
    {
        LogError( "Failed to mount even after formatting...exiting\n" );
        return -1;
    }
    else
    {
        LogInfo( "Successfully mounted\n" );
    }

    /* read current count */
    uint32_t boot_count = 0;
#ifdef LFS_NO_MALLOC
    err = lfs_file_opencfg( &lfs, &file, path, LFS_O_RDWR | LFS_O_CREAT, &xFileConfig );
#else
    err = lfs_file_open( &lfs, &file, path, LFS_O_RDWR | LFS_O_CREAT );
#endif

    if( err )
    {
        LogError( "Failed to open file\n" );
        return -1;
    }

    int lNBytesRead = lfs_file_read( &lfs, &file, &boot_count, sizeof( boot_count ) );

    if( lNBytesRead < 0 )
    {
        LogError( "Failed to read from file '%s'\n", path );
        lfs_file_close( &lfs, &file );
        return -1;
    }
    else
    {
        LogInfo( "Read %d Bytes from '%s'\n", lNBytesRead, path );
    }

    /* update boot count */
    boot_count += 1;
    lfs_file_rewind( &lfs, &file );
    int lNBytesWritten = lfs_file_write( &lfs, &file, &boot_count, sizeof( boot_count ) );

    if( lNBytesWritten < 0 )
    {
        LogError( "Failed to write to file '%s'\n", path );
        lfs_file_close( &lfs, &file );
        return -1;
    }
    else
    {
        LogInfo( "Wrote %d bytes to '%s'\n", lNBytesWritten, path );
    }

    /* remember the storage is not updated until the file is closed successfully */
    lfs_file_close( &lfs, &file );

    /* release any resources we were using */
    lfs_unmount( &lfs );

    /* print the boot count */
    LogInfo( "boot_count: %d\n", boot_count );

    return 0;
}
#endif /* if 0 */
