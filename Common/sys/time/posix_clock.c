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

/* Derived from FreeRTOS POSIX V1.2.1 */

/* C standard library includes. */
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* FreeRTOS Includes */
#include "FreeRTOS.h"
#include "semphr.h"
#include "atomic.h"
#include "posix_utils.h"

#include "clock_config.h"

#include "kvstore.h"


#if MAX_SECONDS_SINCE_1970 > UINT32_MAX
    #define MAX_SECONDS_SINCE_1970_U32      ( UINT32_MAX )
#else
    #define MAX_SECONDS_SINCE_1970_U32      ( MAX_SECONDS_SINCE_1970 )
#endif


#if MIN_SECONDS_SINCE_1970 > 0LL
    #define MIN_SECONDS_SINCE_1970_U32      ( ( uint32_t ) MIN_SECONDS_SINCE_1970 )
#else
    #define MIN_SECONDS_SINCE_1970_U32      0UL
#endif

#ifndef CLOCK_MONOTONIC
    #error "CLOCK_MONOTONIC is required."
#endif /* !defined( CLOCK_MONOTONIC ) */

static struct timespec xLastTimeHwm = { 0 };
static struct timespec xLastTimeHwmMonotonic = { 0 };
static SemaphoreHandle_t xTimeMutex = NULL;

/* Forward declarations */

static inline int clock_gettime_monotonic( struct timespec * tp );
static inline int clock_gettime_hwm( struct timespec * pxNewTimeHwm );
static inline int clock_gettime_hwm_nv( struct timespec * tp );
static inline int clock_settime_hwm( const struct timespec * tp  );
static inline int clock_gettime_rtc( struct timespec * tp );
static inline int clock_settime_rtc( const struct timespec * tp  );


/*-----------------------------------------------------------*/

clock_t clock( void )
{
    /* This function is currently unsupported. It will always return -1. */

    return ( clock_t ) -1;
}

/*-----------------------------------------------------------*/

int clock_getcpuclockid( pid_t pid,
                         clockid_t * clock_id )
{
    /* Silence warnings about unused parameters. */
    ( void ) pid;
    ( void ) clock_id;

    /* This function is currently unsupported. It will always return EPERM. */
    return EPERM;
}

/*-----------------------------------------------------------*/

int clock_getres( clockid_t clock_id,
                  struct timespec * res )
{
    int lError = 0;

    if( res == NULL )
    {
        lError = -1;
        errno = EINVAL;
    }
    else
    {
        switch( clock_id )
        {
            /* CLOCK_HWM has equal resolution to CLOCK_MONOTONIC */
            case CLOCK_HWM:
            case CLOCK_MONOTONIC:
                res->tv_sec = 0;
                res->tv_nsec = NANOSECONDS_PER_TICK;
                break;

            /* CLOCK_REALTIME has a resolution of 1 second */
            case CLOCK_REALTIME:
                res->tv_sec = 1;
                res->tv_nsec = 0;
                break;

            /* CLOCK_HWM_NV has a fixed resolution of 1 second */
            case CLOCK_HWM_NV:
                res->tv_sec = 1;
                res->tv_nsec = 0;
                break;

            default:
                lError = -1;
                errno = EINVAL;
                break;
        }
    }

    return lError;
}

/*-----------------------------------------------------------*/

static inline int clock_gettime_monotonic( struct timespec * tp )
{
    TimeOut_t xCurrentTime = { 0 };

    configASSERT( tp );

    /* Intermediate variable used to convert TimeOut_t to struct timespec.
     * Also used to detect overflow issues. It must be unsigned because the
     * behavior of signed integer overflow is undefined. */
    uint64_t ullTickCount = 0ULL;

    /* Get the current tick count and overflow count. vTaskSetTimeOutState()
     * is used to get these values because they are both static in tasks.c. */
    vTaskSetTimeOutState( &xCurrentTime );

    /* Adjust the tick count for the number of times a TickType_t has overflowed.
     * portMAX_DELAY should be the maximum value of a TickType_t. */
    ullTickCount = ( uint64_t ) ( xCurrentTime.xOverflowCount ) << ( sizeof( TickType_t ) * 8 );

    /* Add the current tick count. */
    ullTickCount += xCurrentTime.xTimeOnEntering;

    /* Convert ullTickCount to timespec. */
    UTILS_NanosecondsToTimespec( ( int64_t ) ullTickCount * NANOSECONDS_PER_TICK, tp );

    return 0;
}

/*-----------------------------------------------------------*/

#ifdef CLOCK_HWM

static inline int clock_gettime_hwm( struct timespec * pxNewTimeHwm )
{
    int lError = 0;

    configASSERT( pxNewTimeHwm );

    if( xSemaphoreTake( xTimeMutex, TIME_MUTEX_MAX_WAIT_TICKS ) )
    {
        struct timespec xTimeMonotonic = { 0 };
        struct timespec xMonotonicTimeDelta = { 0 };

        /* Check if xLastTimeHwm needs to be read from nvm */
        if( ( xLastTimeHwmMonotonic.tv_sec == 0 &&
              xLastTimeHwmMonotonic.tv_nsec == 0 &&
              xLastTimeHwm.tv_sec == 0 &&
              xLastTimeHwm.tv_nsec == 0 ) ||
            UTILS_ValidateTimespec( &xLastTimeHwm ) == false )
        {

#ifdef CLOCK_HWM_NV
            lError = clock_gettime_hwm_nv( &xLastTimeHwm );
#endif /* CLOCK_HWM_NV */

            /* Reset if invalid */
            if( lError != 0 ||
                UTILS_ValidateTimespec( &xLastTimeHwm ) == false )
            {
                xLastTimeHwm.tv_sec = 0;
                xLastTimeHwm.tv_nsec = 0;
                lError = -1;
            }
        }
        /* Validate current xLastTimeHwmMonotonic value */
        else if( UTILS_ValidateTimespec( &xLastTimeHwmMonotonic ) == false )
        {
            lError = -1;
        }
        else
        {
            /* Empty */
        }

        /* Get the current monotonic time */
        if( lError == 0 )
        {
            lError = clock_gettime_monotonic( &xTimeMonotonic );

            configASSERT( lError != 0 || UTILS_ValidateTimespec( &xTimeMonotonic ) );
        }

        /* Calculate delta between now and xLastTimeHwmMonotonic. */
        if( lError == 0 )
        {
            lError = UTILS_TimespecSubtract( &xTimeMonotonic,
                                             &xLastTimeHwmMonotonic,
                                             &xMonotonicTimeDelta );

            configASSERT( lError != 0 || UTILS_ValidateTimespec( &xMonotonicTimeDelta ) );
        }

        /* Update xLastTimeHwm amd copy to the user provided pointer. */
        if( lError == 0 )
        {
            lError = UTILS_TimespecAdd( &xLastTimeHwm,
                                        &xMonotonicTimeDelta,
                                        pxNewTimeHwm );

            configASSERT( lError != 0 || UTILS_ValidateTimespec( pxNewTimeHwm ) );
        }

        if( lError == 0 )
        {
            xLastTimeHwmMonotonic = xTimeMonotonic;
            xLastTimeHwm = *pxNewTimeHwm;
        }
        else
        {
            pxNewTimeHwm->tv_nsec = 0;
            pxNewTimeHwm->tv_sec = 0;
        }

        /* Release the mutex */
        ( void ) xSemaphoreGive( xTimeMutex );
    }
    else
    {
        lError = -1;
    }

    if( lError != 0 )
    {
        errno = EINVAL;
    }
    return lError;
}

/*-----------------------------------------------------------*/
#ifdef CLOCK_HWM_NV

static inline int clock_gettime_hwm_nv( struct timespec * tp )
{
    int lError = 0;
    uint32_t ulSeconds1970 = KVStore_getUInt32( CS_TIME_HWM_S_1970, NULL );

    configASSERT( tp );

    if( tp == NULL ||
        ulSeconds1970 == 0 ||
        ulSeconds1970 == UINT32_MAX )
    {
        lError = -1;
        errno = EINVAL;
    }
    else
    {
        tp->tv_nsec = 0;
        tp->tv_sec = ( time_t ) ulSeconds1970;
    }
    return lError;
}

#endif /* CLOCK_HWM_NV */
/*-----------------------------------------------------------*/

static inline int clock_settime_hwm( const struct timespec * tp  )
{
    int lError = 0;

    configASSERT( tp );

    if( xSemaphoreTake( xTimeMutex, TIME_MUTEX_MAX_WAIT_TICKS ) )
    {
#ifdef CLOCK_HWM_NV
        /* A UINT32 can store from Jan 1, 1970 to Feb 6, 2106 */
        if( tp->tv_sec >= MIN_SECONDS_SINCE_1970_U32 &&
            tp->tv_sec <= MAX_SECONDS_SINCE_1970_U32 )
        {
            uint32_t ulSeconds1970 = ( uint32_t ) tp->tv_sec;

            if( KVStore_setUInt32( CS_TIME_HWM_S_1970, ulSeconds1970 ) == pdFALSE )
            {
                lError = -1;
            }
        }
#endif /* CLOCK_HWM_NV */

        if( tp->tv_sec >= MIN_SECONDS_SINCE_1970 &&
            tp->tv_sec <= MAX_SECONDS_SINCE_1970 )
        {
            if( clock_gettime_monotonic( &xLastTimeHwmMonotonic ) == 0 )
            {
                xLastTimeHwm = *tp;
            }
            else
            {
                lError = -1;
            }
        }
        ( void ) xSemaphoreGive( xTimeMutex );
    }
    else
    {
        lError = -1;
        errno = EINVAL;
    }
    return lError;
}

#endif /* CLOCK_HWM */

/*-----------------------------------------------------------*/

#ifdef CLOCK_REALTIME

static inline int clock_gettime_rtc( struct timespec * tp )
{
    int lError = 0;
    struct tm xTime = { 0 };

    configASSERT( tp );

    if( pxHndlRtc == NULL )
    {
        lError = -1;
        errno = EPERM;
    }
    else if( xSemaphoreTake( xTimeMutex, TIME_MUTEX_MAX_WAIT_TICKS ) == pdTRUE )
    {
        HAL_StatusTypeDef xHalStatus = HAL_OK;
        RTC_TimeTypeDef xHalTime = { 0 };
        RTC_DateTypeDef xHalDate = { 0 };

        xHalStatus = HAL_RTC_GetTime( pxHndlRtc, &xHalTime, RTC_FORMAT_BIN );
        if( xHalStatus == HAL_OK )
        {
            /* DST is not used */
            xTime.tm_isdst = 0;

            /* Seconds after the minute [0,59] -> [0,60] ( C99 ) */
            xTime.tm_sec = xHalTime.Seconds;

            configASSERT( xHalTime.Seconds <= 59 );
            configASSERT( xTime.tm_sec >= 0 &&
                          xTime.tm_sec <= 60 );

            /* Minutes After the Hour [0,59] -> [0,59] */
            xTime.tm_min =  xHalTime.Minutes;

            configASSERT( xHalTime.Minutes <= 59 );
            configASSERT( xTime.tm_min >= 0 &&
                          xTime.tm_min <= 59 );

            /* Hours since midnight [0,23] -> [0,23] */
            xTime.tm_hour = xHalTime.Hours;

            configASSERT( xHalTime.Hours <= 23 );
            configASSERT( xTime.tm_hour >= 0 &&
                          xTime.tm_hour <= 23 );
        }
        else
        {
            lError = -1;
            errno = EINVAL;
        }

        /* HAL_RTC_GetDate must always be called after HAL_RTC_GetTime */
        xHalStatus = HAL_RTC_GetDate( pxHndlRtc, &xHalDate, RTC_FORMAT_BIN );
        if( xHalStatus == HAL_OK )
        {
            /* tm_wday and tm_yday are ignored by mktime */
            xTime.tm_wday = 0;
            xTime.tm_yday = 0;

            /* tm_year Years since 1900 */
            /* [0,99] -> [2020,] */
            xTime.tm_year = CLOCK_RTC_YEAR_0 + xHalDate.Year;

            configASSERT( xHalDate.Year < 99 );
            configASSERT( xTime.tm_year >= CLOCK_RTC_YEAR_0 &&
                          xTime.tm_year <= CLOCK_RTC_YEAR_0 + 99 );

            /* tm_mon: Months since January [ 0, 11 ] */

            switch( xHalDate.Month )
            {
                case RTC_MONTH_JANUARY:
                case RTC_MONTH_FEBRUARY:
                case RTC_MONTH_MARCH:
                case RTC_MONTH_APRIL:
                case RTC_MONTH_MAY:
                case RTC_MONTH_JUNE:
                case RTC_MONTH_JULY:
                case RTC_MONTH_AUGUST:
                case RTC_MONTH_SEPTEMBER:
                    xTime.tm_mon = xHalDate.Month - RTC_MONTH_JANUARY;
                    break;
                case RTC_MONTH_OCTOBER:
                case RTC_MONTH_NOVEMBER:
                case RTC_MONTH_DECEMBER:
                    xTime.tm_mon = 10 + ( xHalDate.Month & 0x0F );
                    break;
                default:
                    configASSERT( xHalDate.Month >= RTC_MONTH_JANUARY &&
                                  xHalDate.Month <= RTC_MONTH_DECEMBER );

                    configASSERT( xHalDate.Month <= RTC_MONTH_SEPTEMBER ||
                                  xHalDate.Month >= RTC_MONTH_OCTOBER );
                    lError = -1;
                    break;
            }
            xTime.tm_mon = xHalDate.Month;
            configASSERT( xTime.tm_mon >= 0 &&
                          xTime.tm_mon <= 11 );

            /* Day of the Month [ 1, 31 ] */
            xTime.tm_mday = xHalDate.Date;

            configASSERT( xHalDate.Date >= 1 &&
                          xHalDate.Date <= 31 );
        }
        else
        {
            lError = -1;
            errno = EINVAL;
        }

        ( void ) xSemaphoreGive( xTimeMutex );
    }
    else
    {
        lError = -1;
        errno = EPERM;
    }

    if( lError == 0 )
    {
        time_t xSecondsSinceEpoc = mktime( &xTime );
        if( xSecondsSinceEpoc >= MIN_SECONDS_SINCE_1970 &&
            xSecondsSinceEpoc <= MAX_SECONDS_SINCE_1970 )
        {
            tp->tv_nsec = 0;
            tp->tv_sec = xSecondsSinceEpoc;
        }
        else
        {
            lError = -1;
            errno = EINVAL;
        }
    }
    return lError;
}

/*-----------------------------------------------------------*/

static inline int clock_settime_rtc( const struct timespec * tp  )
{
    int lError = 0;
    struct tm xTime = { 0 };

    if( pxHndlRtc == NULL )
    {
        lError = -1;
        errno = EPERM;
    }

    if( lError == 0 &&
        gmtime_r( &( tp->tv_sec ), &xTime ) == NULL )
    {
        lError = -1;
        errno = EINVAL;
    }

    if( lError == 0 )
    {
        lError = xSemaphoreTake( xTimeMutex, TIME_MUTEX_MAX_WAIT_TICKS ) ? 0 : -1;
    }

    if( lError == 0 )
    {
        HAL_StatusTypeDef xHalStatus = HAL_OK;
        RTC_DateTypeDef xHalDate;

        xHalDate.Date = xTime.tm_mday;           /* tm_mday is 1 - 31 */
        xHalDate.Month = xTime.tm_mon + 1U;      /* tm_mon is 0 - 11, must map to 1 - 12 */
        xHalDate.WeekDay = xTime.tm_wday + 1U;   /* tm_wday is 0 - 6, must map to 1 - 7 */
        xHalDate.Year = xTime.tm_year % 100U;    /* tm_year is years since 1900. maps to 0-99 */

        xHalStatus = HAL_RTC_SetDate( pxHndlRtc, &xHalDate, RTC_FORMAT_BIN );

        if( xHalStatus == HAL_OK )
        {
            RTC_TimeTypeDef xHalTime;

            /* StoreOperation and DayLightSaving parameters are deprecated
             * SecondFraction is not used by SetTime*/
            xHalTime.Hours = xTime.tm_hour;     /* 0 - 23 */
            xHalTime.Minutes = xTime.tm_min;    /* 0 - 59 */
            xHalTime.Seconds = xTime.tm_sec;    /* 0 - 59 */
            xHalTime.TimeFormat = RTC_HOURFORMAT_24;
            xHalTime.SubSeconds = 0;

            xHalStatus = HAL_RTC_SetTime( pxHndlRtc, &xHalTime, RTC_FORMAT_BIN );
        }

        lError = ( xHalStatus == HAL_OK ) ? 0 : -1;

        ( void ) xSemaphoreGive( xTimeMutex );
    }
    return lError;
}

#endif /* CLOCK_REALTIME */

/*-----------------------------------------------------------*/

int clock_gettime( clockid_t clock_id,
                   struct timespec * tp )
{
    int lError = 0;

    if( tp == NULL )
    {
        lError = -1;
    }
    else if( xTimeMutex == NULL )
    {
        ATOMIC_ENTER_CRITICAL();
        if( xTimeMutex == NULL )
        {
            xTimeMutex = xSemaphoreCreateMutex();
        }
        ATOMIC_EXIT_CRITICAL();

        if( xTimeMutex == NULL )
        {
            lError = -1;
            errno = EPERM;
        }
    }
    else
    {
        switch( clock_id )
        {
#ifdef CLOCK_REALTIME
            case CLOCK_REALTIME:
                lError = clock_gettime_rtc( tp );
#ifdef CLOCK_HWM
                /* If the RTC failed, estimate from HWM */
                if( lError != 0 )
                {
                    lError = clock_gettime_hwm( tp );
                }
#endif /* CLOCK_HWM */
#endif /* CLOCK_REALTIME */
                break;

#ifdef CLOCK_HWM
            case CLOCK_HWM:
                lError = clock_gettime_hwm( tp );
                break;
#endif /* CLOCK_HWM */

#ifdef CLOCK_MONOTONIC
            case CLOCK_MONOTONIC:
                lError = clock_gettime_monotonic( tp );
                break;
#endif /* CLOCK_MONOTONIC */

            default:
                lError = -1;
                errno = EINVAL;
                break;
        }
        switch( clock_id )
        {
#ifdef CLOCK_MONOTONIC
            case CLOCK_MONOTONIC:
                lError = clock_gettime_monotonic( tp );
                break;
    #endif
            case CLOCK_REALTIME:
                lError = clock_gettime_rtc( tp );

                /* If the RTC failed, estimate from HWM */
                if( lError != 0 )
                {
                    lError = clock_gettime_hwm( tp );
                }
                break;

            case CLOCK_HWM:
                lError = clock_gettime_hwm( tp );
                break;

            default:
                lError = -1;
                errno = EINVAL;
                break;
        }
    }

    return lError;
}

/*-----------------------------------------------------------*/

int clock_settime( clockid_t clock_id,
                   const struct timespec * tp )
{
    int lError = 0;

    if( tp == NULL ||
        UTILS_ValidateTimespec( tp ) == false ||
        tp->tv_sec < MIN_SECONDS_SINCE_1970 ||
        tp->tv_sec > MAX_SECONDS_SINCE_1970 )
    {
        lError = -1;
        errno = EINVAL;
    }
    else if( xTimeMutex == NULL )
    {
        ATOMIC_ENTER_CRITICAL();
        if( xTimeMutex == NULL )
        {
            xTimeMutex = xSemaphoreCreateMutex();
        }
        ATOMIC_EXIT_CRITICAL();

        if( xTimeMutex == NULL )
        {
            lError = -1;
            errno = EPERM;
        }
    }
    else
    {
        switch( clock_id )
        {
        #ifdef CLOCK_REALTIME
            case CLOCK_REALTIME:
                lError = clock_settime_rtc( tp );
                break;
#endif /* CLOCK_REALTIME */

#ifdef CLOCK_HWM
            case CLOCK_HWM:
                lError = clock_settime_hwm( tp );
                break;
#endif /* CLOCK_HWM */

#ifdef CLOCK_MONOTONIC
            case CLOCK_MONOTONIC:
                /* Intentional fall-through */

#endif /* CLOCK_MONOTONIC */

            default:
                lError = -1;
                errno = EINVAL;
                break;
        }
    }

    if( lError == 0 &&
        UTILS_ValidateTimespec( tp ) == false )
    {
        lError = -1;
        errno = EINVAL;
    }

    return lError;
}

/*-----------------------------------------------------------*/
