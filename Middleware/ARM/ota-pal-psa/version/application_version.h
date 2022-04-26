/*
 * Copyright (c) 2020-2021 Arm Limited. All rights reserved.
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
 */

#ifndef APPLICATION_VERSION_H_
#define APPLICATION_VERSION_H_

#include "ota_appversion32.h"

/**
 * @brief Get the running image version of the given image type.
 *
 * Get the image version by PSA Firmware update service API and assign it to xAppFirmwareVersion
 * which is use in the ota agent.
 *
 * @note portALLOCATE_SECURE_CONTEXT( 0 ) should be called before this function, otherwise this function
 * will always fail.
 * @param[in] N/A.
 *
 * @return 0 on success and the xAppFirmwareVersion is assigned with the value read from the Firmware
 * update service. -1 on failure and the xAppFirmwareVersion is 0.
 */
int GetImageVersionPSA( uint8_t ucImageType );

#endif
