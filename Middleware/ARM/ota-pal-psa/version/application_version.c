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

/**
 * @file application_version.c
 * @brief Get the image version which is used by the AWS OTA
 *
 */

/* C Runtime includes. */
#include <stdlib.h>

/* Amazon FreeRTOS include. */
#include "FreeRTOS.h"
#include "application_version.h"

/* TF-M Firmware Update service. */
#include "psa/update.h"

AppVersion32_t appFirmwareVersion;

int GetImageVersionPSA( uint8_t ucImageType )
{
    psa_image_info_t xImageInfo = { 0 };
    psa_status_t uxStatus;
    psa_image_id_t ulImageID = TFM_FWU_INVALID_IMAGE_ID;

    /* Get the version information of the full image in primary slot. */
    ulImageID = FWU_CALCULATE_IMAGE_ID( FWU_IMAGE_ID_SLOT_ACTIVE, ucImageType, 0 );
    uxStatus = psa_fwu_query( ulImageID, &xImageInfo );
    if( uxStatus == PSA_SUCCESS )
    {
        appFirmwareVersion.u.x.major = xImageInfo.version.iv_major;
        appFirmwareVersion.u.x.minor = xImageInfo.version.iv_minor;
        appFirmwareVersion.u.x.build = (uint16_t)xImageInfo.version.iv_build_num;
        return 0;
    }
    else
    {
        appFirmwareVersion.u.signedVersion32 = 0;
        return -1;
    }
}
