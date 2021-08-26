#ifndef _LITTLE_FS_PORT_H_
#define _LITTLE_FS_PORT_H_

#include <stddef.h>
#include "lfs.h"

#define CONFIG_SIZE_LOOKAHEAD_BUFFER 16
#define CONFIG_SIZE_CACHE_BUFFER 16

struct lfs_config const * lfs_port_get_config( void );

lfs_t * lfs_port_get_fs_handle( void );

// Read a region in a block. Negative error codes are propogated
// to the user.
int lfs_port_bd_read( const struct lfs_config *c, lfs_block_t block,
        			  lfs_off_t off, void *buffer, lfs_size_t size );

// Program a region in a block. The block must have previously
// been erased. Negative error codes are propogated to the user.
// May return LFS_ERR_CORRUPT if the block should be considered bad.
int lfs_port_bd_prog( const struct lfs_config *c, lfs_block_t block,
        			  lfs_off_t off, const void *buffer, lfs_size_t size );

// Erase a block. A block must be erased before being programmed.
// The state of an erased block is undefined. Negative error codes
// are propogated to the user.
// May return LFS_ERR_CORRUPT if the block should be considered bad.
int lfs_port_bd_erase( const struct lfs_config *c, lfs_block_t block );

// Sync the state of the underlying block device. Negative error codes
// are propogated to the user.
int lfs_port_bd_sync( const struct lfs_config *c );

// Lock the underlying block device. Negative error codes
// are propogated to the user.
int lfs_port_bd_lock( const struct lfs_config *c );

// Unlock the underlying block device. Negative error codes
// are propogated to the user.
int lfs_port_bd_unlock( const struct lfs_config *c );

// Demo/Verification. Mounts, formats as necessary, and read/updates a file "boot_count"
// Returns 0 for success, else failure.
int lfs_port_demo( void );

#endif
