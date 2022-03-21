/*
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

#ifndef _KVSTORE_H
#define _KVSTORE_H

#include "FreeRTOS.h"
#include <stddef.h>

typedef enum KVStoreKey
{
    KV_TYPE_NONE = 0,
    KV_TYPE_BASE_T,
    KV_TYPE_UBASE_T,
    KV_TYPE_INT32,
    KV_TYPE_UINT32,
    KV_TYPE_STRING,
    KV_TYPE_BLOB,
    KV_TYPE_LAST
} KVStoreValueType_t;

#define KV_DFLT_KV_TYPE_BASE_T( value ) \
    { KV_TYPE_BASE_T, sizeof( value ), .bt = value }

#define KV_DFLT_KV_TYPE_UBASE_T( value ) \
    { KV_TYPE_UBASE_T, sizeof( value ), .ubt = value }

#define KV_DFLT_KV_TYPE_INT32( value ) \
    { KV_TYPE_INT32, sizeof( value ), .i32 = value }

#define KV_DFLT_KV_TYPE_UINT32( value ) \
    { KV_TYPE_UINT32, sizeof( value ), .u32 = value }

#define KV_DFLT_KV_TYPE_STRING( value ) \
    { KV_TYPE_STRING, sizeof( value ), .str = value }

#define KV_DFLT_KV_TYPE_BLOB( value ) \
    { KV_TYPE_BLOB, sizeof( value ), .blob = value }

#define KV_DFLT( type, value )    KV_DFLT_ ## type( value )

#include "kvstore_config.h"

extern const char * const kvStoreKeyMap[ CS_NUM_KEYS ];

typedef enum KvStoreEnum KVStoreKey_t;

/* Public function definitions */
void KVStore_init( void );

size_t KVStore_getSize( KVStoreKey_t key );

KVStoreValueType_t KVStore_getType( KVStoreKey_t key );

size_t KVStore_getBlob( KVStoreKey_t key,
                        void * pvBuffer,
                        size_t xMaxLength );
void * KVStore_getBlobHeap( KVStoreKey_t key,
                            size_t * pxLength );
BaseType_t KVStore_setBlob( KVStoreKey_t key,
                            size_t xLength,
                            const void * pvNewValue );

size_t KVStore_getString( KVStoreKey_t key,
                          char * pvBuffer,
                          size_t xMaxLength );
char * KVStore_getStringHeap( KVStoreKey_t key,
                              size_t * pxLength );
BaseType_t KVStore_setString( KVStoreKey_t key,
                              const char * pcNewValue );

uint32_t KVStore_getUInt32( KVStoreKey_t key,
                            BaseType_t * pxSuccess );
BaseType_t KVStore_setUInt32( KVStoreKey_t key,
                              uint32_t ulNewVal );

int32_t KVStore_getInt32( KVStoreKey_t key,
                          BaseType_t * pxSuccess );
BaseType_t KVStore_setInt32( KVStoreKey_t key,
                             int32_t lNewVal );

UBaseType_t KVStore_getUBase( KVStoreKey_t key,
                              BaseType_t * pxSuccess );
BaseType_t KVStore_setUBase( KVStoreKey_t key,
                             UBaseType_t uxNewVal );

BaseType_t KVStore_getBase( KVStoreKey_t key,
                            BaseType_t * pxSuccess );
BaseType_t KVStore_setBase( KVStoreKey_t key,
                            BaseType_t xNewVal );

const char * kvKeyToString( KVStoreKey_t xKey );
KVStoreKey_t kvStringToKey( const char * pcKey );

BaseType_t KVStore_xCommitChanges( void );

#endif /* _KVSTORE_H */
