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

#ifndef _CLOCK_CONFIG
#define _CLOCK_CONFIG

#ifndef _POSIX_TIMERS
#define _POSIX_TIMERS
#endif /* _POSIX_TIMERS */

#include <time.h>

#ifndef CLOCK_REALTIME
    #define CLOCK_REALTIME          ( ( clockid_t ) 1 )
#endif

#ifndef CLOCK_MONOTONIC
    #define CLOCK_MONOTONIC         ( ( clockid_t ) 4 )
#endif

#define CLOCK_HWM                   ( ( clockid_t ) 0xFF )
#define CLOCK_HWM_NV                ( ( clockid_t ) 0xFFFF )

#define TIME_MUTEX_MAX_WAIT_TICKS   pdMS_TO_TICKS( 500 )

#define MICROSECONDS_PER_SECOND     ( 1000000LL )
#define NANOSECONDS_PER_SECOND      ( 1000000000LL )
#define NANOSECONDS_PER_TICK        ( NANOSECONDS_PER_SECOND / configTICK_RATE_HZ )

#define TIMESTAMP_JAN_01_2500       ( 16725254400LL )

#define MIN_SECONDS_SINCE_1970      0L
#define MAX_SECONDS_SINCE_1970      TIMESTAMP_JAN_01_2500

#define CLOCK_RTC_YEAR_0            ( 2020U )

#define TIME_T_INVALID              ( ( time_t ) -1 )

#endif /* _CLOCK_CONFIG */
