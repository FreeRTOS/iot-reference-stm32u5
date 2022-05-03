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

#ifndef _KVSTORE_CONFIG_PLAT_H
#define _KVSTORE_CONFIG_PLAT_H

/* Define KV_STORE_CACHE_ENABLE to 1 to enable an in-memory cache of all Key / Value pairs */
#define KV_STORE_CACHE_ENABLE       1

/* Define KV_STORE_NVIMPL_ENABLE to 1 to enable storage of all key / value pairs in non-volatile storage */
#define KV_STORE_NVIMPL_ENABLE      1

#define KV_STORE_NVIMPL_LITTLEFS    1

#define KV_STORE_NVIMPL_ARM_PSA     0

#define KVSTORE_KEY_MAX_LEN         16
#define KVSTORE_VAL_MAX_LEN         256

#endif /* _KVSTORE_CONFIG_PLAT_H */
