/*
 * FreeRTOS STM32 Reference Integration
 * Copyright (C) 2022 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

#ifndef TLS_TRANSPORT_CONFIG
#define TLS_TRANSPORT_CONFIG

#include "tls_transport_lwip.h"
#include "core_pkcs11_config.h"

#define configTLS_MAX_LABEL_LEN    pkcs11configMAX_LABEL_LENGTH
#define TLS_KEY_PRV_LABEL          pkcs11_TLS_KEY_PRV_LABEL
#define TLS_KEY_PUB_LABEL          pkcs11_TLS_KEY_PUB_LABEL
#define TLS_CERT_LABEL             pkcs11_TLS_CERT_LABEL
#define TLS_ROOT_CA_CERT_LABEL     pkcs11_ROOT_CA_CERT_LABEL
#define OTA_SIGNING_KEY_LABEL      pkcs11configLABEL_CODE_VERIFICATION_KEY

#define TRANSPORT_USE_CTR_DRBG     1

/*
 * Define MBEDTLS_TRANSPORT_PKCS11 to enable certificate and key storage via the PKCS#11 API.
 */
#define MBEDTLS_TRANSPORT_PKCS11

/*
 * Define MBEDTLS_TRANSPORT_PSA to enable certificate and key storage via the ARM PSA API.
 */
/*#define MBEDTLS_TRANSPORT_PSA */


#endif /* TLS_TRANSPORT_CONFIG */
