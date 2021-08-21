#include "lfs_port.h"

#include "stm32u585xx.h"
#include "stm32u5xx.h"
#include "stm32u5xx_hal_flash.h"
#include "stm32u5xx_hal_flash_ex.h"

/* Ported to use all pages of Bank 2
 *
 */


// Use second bank to avoid program flash. TODO: Should add variable in linker script to mark end of program flash
#define CONFIG_LFS_FLASH_BASE ( FLASH_BASE + FLASH_BANK_SIZE )

#ifdef LFS_NO_MALLOC
	static uint8_t __ALIGN_BEGIN ucReadBuffer[CONFIG_SIZE_CACHE_BUFFER] __ALIGN_END = { 0 };
	static uint8_t __ALIGN_BEGIN ucProgBuffer[CONFIG_SIZE_CACHE_BUFFER] __ALIGN_END = { 0 };
	static uint8_t __ALIGN_BEGIN ucLookAheadBuffer[CONFIG_SIZE_LOOKAHEAD_BUFFER] __ALIGN_END = { 0 };
#endif

static const struct lfs_config cfg =
{
	.read   = lfs_port_bd_read,
	.prog   = lfs_port_bd_prog,
	.erase  = lfs_port_bd_erase,
	.sync   = lfs_port_bd_sync,
	.lock   = lfs_port_bd_lock,
	.unlock = lfs_port_bd_unlock,

	// block device configuration
	.read_size = 1,
	.prog_size = 16,
	.block_size = FLASH_PAGE_SIZE,
	.block_count = 128,
	.cache_size = CONFIG_SIZE_CACHE_BUFFER,
	.lookahead_size = CONFIG_SIZE_LOOKAHEAD_BUFFER,
#ifdef LFS_NO_MALLOC
	.read_buffer = ucReadBuffer,
	.prog_buffer = ucProgBuffer,
	.lookahead_buffer = ucLookAheadBuffer,
#endif
	.block_cycles = 500
};



struct lfs_config const * lfs_port_get_config( void )
{
	return &cfg;
}

int lfs_port_bd_read( const struct lfs_config *c, lfs_block_t block,
        			  lfs_off_t off, void *buffer, lfs_size_t size )
{
	HAL_FLASH_Unlock();
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

	uint32_t src_address = CONFIG_LFS_FLASH_BASE + block * cfg.block_size + off;
	memcpy( buffer, (void *)src_address, size );

	HAL_FLASH_Lock();

	return 0;
}

int lfs_port_bd_prog( const struct lfs_config *c, lfs_block_t block,
        			  lfs_off_t off, const void *buffer, lfs_size_t size )
{
	HAL_StatusTypeDef xHAL_Status = HAL_OK;
	uint32_t n_rows = size / cfg.prog_size;
	uint32_t dest_address = 0;
	uint32_t src_address = 0;
	uint32_t block_base_addr = CONFIG_LFS_FLASH_BASE + block * cfg.block_size;

	HAL_FLASH_Unlock();
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
	for( uint32_t i_row=0; i_row<n_rows; i_row++ )
	{
		dest_address = block_base_addr + off + i_row*4*sizeof(uint32_t);
		src_address = (uint32_t)buffer + i_row*4*sizeof(uint32_t);
		xHAL_Status = HAL_FLASH_Program( FLASH_TYPEPROGRAM_QUADWORD, dest_address, src_address);
		if( xHAL_Status != HAL_OK )
		{
			HAL_FLASH_Lock();
			return -1;
		}
	}
	HAL_FLASH_Lock();

	return 0;
}

int lfs_port_bd_erase( const struct lfs_config *c, lfs_block_t block )
{
	uint32_t ulPageError = 0;
	FLASH_EraseInitTypeDef xErase_Config = { 0 };

	xErase_Config.TypeErase = FLASH_TYPEERASE_PAGES;
	xErase_Config.Banks = FLASH_BANK_2;
	xErase_Config.Page = block;
	xErase_Config.NbPages = 1;

	HAL_FLASH_Unlock();
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
	HAL_StatusTypeDef xHAL_Status = HAL_FLASHEx_Erase( &xErase_Config, &ulPageError );
	HAL_FLASH_Lock();

	return xHAL_Status == HAL_OK ? 0 : -1;
}

int lfs_port_bd_sync( const struct lfs_config *c )
{
	return 0;
}

int lfs_port_bd_lock( const struct lfs_config *c )
{
	HAL_StatusTypeDef xHAL_Status = HAL_FLASH_Lock();

	return xHAL_Status == HAL_OK ? 0 : -1;
}

int lfs_port_bd_unlock( const struct lfs_config *c )
{
	HAL_StatusTypeDef xHAL_Status = HAL_FLASH_Unlock();

	return xHAL_Status == HAL_OK ? 0 : -1;

}

#ifdef LFS_CONFIG
// Software CRC implementation with small lookup table
uint32_t lfs_crc(uint32_t crc, const void *buffer, size_t size) {
    static const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };

    const uint8_t *data = buffer;

    for (size_t i = 0; i < size; i++) {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }

    return crc;
}
#endif

int lfs_port_demo( void )
{
	lfs_t lfs = { 0 };
	lfs_file_t file = { 0 };
	const char * path = "boot_count";

#ifdef LFS_NO_MALLOC
	static uint8_t __ALIGN_BEGIN ucFileCache[CONFIG_SIZE_CACHE_BUFFER] __ALIGN_END = { 0 };
	struct lfs_file_config xFileConfig = { 0 };

	xFileConfig.buffer = (void *)ucFileCache;
#endif

	struct lfs_config * pCfg = lfs_port_get_config();

    // mount the filesystem
    int err = lfs_mount(&lfs, pCfg);

    // reformat if we can't mount the filesystem
    // this should only happen on the first boot
    if (err) {
    	printf("Failed to mount partition. Reformatting...\n");
        lfs_format(&lfs, pCfg);
        err = lfs_mount(&lfs, pCfg);
    }

    if(err)
    {
    	printf("Failed to mount even after formatting...exiting\n");
		return -1;
    }
    else
    {
    	printf("Successfully mounted\n");
    }

    // read current count
    uint32_t boot_count = 0;
#ifdef LFS_NO_MALLOC
    err = lfs_file_opencfg(&lfs, &file, path, LFS_O_RDWR | LFS_O_CREAT, &xFileConfig);
#else
    err = lfs_file_open(&lfs, &file, path, LFS_O_RDWR | LFS_O_CREAT);
#endif
    if(err)
    {
    	printf("Failed to open file\n");
    	return -1;
    }

    int lNBytesRead = lfs_file_read(&lfs, &file, &boot_count, sizeof(boot_count));
    if( lNBytesRead < 0 )
    {
    	printf("Failed to read from file '%s'\n", path);
        lfs_file_close(&lfs, &file);
        return -1;
    }
    else
    {
        printf("Read %d Bytes from '%s'\n", lNBytesRead, path);
    }

    // update boot count
    boot_count += 1;
    lfs_file_rewind(&lfs, &file);
    int lNBytesWritten = lfs_file_write(&lfs, &file, &boot_count, sizeof(boot_count));
    if( lNBytesWritten < 0 )
    {
    	printf("Failed to write to file '%s'\n", path);
        lfs_file_close(&lfs, &file);
    	return -1;
    }
    else
    {
    	printf("Wrote %d bytes to '%s'\n", lNBytesWritten, path);
    }

    // remember the storage is not updated until the file is closed successfully
    lfs_file_close(&lfs, &file);

    // release any resources we were using
    lfs_unmount(&lfs);

    // print the boot count
    printf("boot_count: %d\n", boot_count);

    return 0;
}
