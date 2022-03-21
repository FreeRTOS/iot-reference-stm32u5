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

#ifndef _KVSTORE_PRV_H
#define _KVSTORE_PRV_H

#include "kvstore_config_plat.h"
#include "kvstore.h"

/* Private Types */

typedef struct
{
    const KVStoreValueType_t type;
    const size_t length;
    union
    {
        const uint32_t u32;
        const int32_t i32;
        const BaseType_t bt;
        const UBaseType_t ubt;
        const void * const blob;
        const char * const str;
    };
} KVStoreDefaultEntry_t;

extern const KVStoreDefaultEntry_t kvStoreDefaults[ CS_NUM_KEYS ];

/* Private functions for NVM implementation */

#if KV_STORE_NVIMPL_ENABLE
size_t xprvGetValueLengthFromImpl( KVStoreKey_t xKey );

BaseType_t xprvReadValueFromImplStatic( KVStoreKey_t xKey,
                                        KVStoreValueType_t * pxType,
                                        size_t * pxLength,
                                        void * pvBuffer,
                                        size_t xBufferSize );

BaseType_t xprvReadValueFromImpl( KVStoreKey_t xKey,
                                  KVStoreValueType_t * pxType,
                                  size_t * pxLength,
                                  void * pvBuffer,
                                  size_t xBufferSize );

BaseType_t xprvWriteValueToImpl( KVStoreKey_t xKey,
                                 KVStoreValueType_t xType,
                                 size_t xLength,
                                 const void * pvData );

void vprvNvImplInit( void );

#endif /* KV_STORE_NVIMPL_ENABLE */


/* Cache related private functions */
#if KV_STORE_CACHE_ENABLE
BaseType_t xprvCopyValueFromCache( KVStoreKey_t key,
                                   KVStoreValueType_t * pxDataType,
                                   size_t * pxDataLength,
                                   void * pvBuffer,
                                   size_t xBufferSize );

BaseType_t xprvWriteCacheEntry( KVStoreKey_t xKey,
                                KVStoreValueType_t xNewType,
                                size_t xLength,
                                const void * pvNewValue );

void vprvCacheInit( void );

size_t prvGetCacheEntryLength( KVStoreKey_t xKey );
KVStoreValueType_t prvGetCacheEntryType( KVStoreKey_t xKey );

#endif /* KV_STORE_CACHE_ENABLE */

#endif /* _KVSTORE_PRV_H */
