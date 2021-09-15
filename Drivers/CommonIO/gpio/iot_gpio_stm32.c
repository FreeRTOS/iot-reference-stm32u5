/*
 * FreeRTOS Common IO V0.1.1
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file iot_gpio.c
 * @brief HAL GPIO implementation on NRF52840 Development Kit. Note, CommonIO GPIO pin-index-space
 *        is separate from board-specific pin index space. User is expected to provide a 1-to-1 mapping
 *        between these spaces.
 *
 */

#include "FreeRTOS.h"

/* STM Board includes. */
#include "stm32u5xx_hal_gpio.h"
#include "stm32u5xx_hal_gpio_ex.h"
#include "stm32u5xx_ll_gpio.h"
#include "stm32u585xx.h"

#include "iot_gpio_stm32_prv.h"

/* Common IO includes */
#include "iot_gpio.h"
#include <stdbool.h>
//#include "iot_gpio_stm32u5_iot_board.h"

/* Note, this depends on logging task being created and running */
#if ( IOT_GPIO_LOGGING_ENABLED == 1 )
	#include "iot_logging_task.h"
    #define IOT_GPIO_MODULE_NAME    "[CommonIO][GPIO]"
    #define IOT_GPIO_LOGF( format, ... )    vLoggingPrintf( IOT_GPIO_MODULE_NAME format, __VA_ARGS__ )
    #define IOT_GPIO_LOG( msg )             vLoggingPrintf( IOT_GPIO_MODULE_NAME msg )
#else
    #define IOT_GPIO_LOGF( format, ... )
    #define IOT_GPIO_LOG( msg )
#endif

#ifndef IOT_GPIO_INTERRUPT_PRIORITY
	#define IOT_GPIO_INTERRUPT_PRIORITY 3
#endif

/*
 * A 1-to-1 mapping set by iot_gpio_config.h. CommonIO Gpio pin index space uses the index i
 * and the board specific identifier is stored at GpioMap[i]. The mapping is user configured.
 */
static const IotMappedPin_t * pxGpioMap;
static IotGpioDescriptor_t * pxGpioDesc;

static IotGpioDescriptor_t * interrupt_to_gpio_map[ NUM_GPIO_PER_CONTROLLER ] = { 0 };

//static const IotGpioDescriptor_t xDefaultGpioDesc =
//{
//    .lGpioNumber        = -1,
//    .xConfig            =
//    {
//        .xDirection     = eGpioDirectionInput,
//        .xOutMode       = eGpioPushPull,
//        .lSpeed	        = GPIO_SPEED_FREQ_MEDIUM,
//        .xPull          = eGpioPullNone,
//        .xInterruptMode = eGpioInterruptNone,
//		.lFunction	    = 0u
//    },
//    .xUserCallback      = NULL,
//    .pvUserContext      = NULL,
//    .ucState            = IOT_GPIO_CLOSED
//};

/*---------------------------------------------------------------------------------*
*                                Private Helpers                                  *
*---------------------------------------------------------------------------------*/

/*
 * @brief   Used to validate whether a CommonIO pin index is a valid argument for iot_gpio_open.
 *          This lGpioNumber will be used to index xGpioDesc and pxGpioMap.
 *
 * @param[in] lGpioNumber CommonIO pin index to validate
 *
 * @return    true if pin is okay to use with iot_gpio_open, else false
 */
static bool prvIsValidPinIndex( int32_t lGpioNumber )
{
    int32_t ulNumberOfMappings = sizeof( pxGpioMap ) == 0 ? 0 : sizeof( pxGpioMap ) / sizeof( pxGpioMap[ 0 ] );

    return ( ulNumberOfMappings > 0 ) &&
           ( ulNumberOfMappings == IOT_COMMON_IO_GPIO_NUMBER_OF_PINS ) &&
           ( 0 <= lGpioNumber && lGpioNumber < ulNumberOfMappings );
}


/*
 * @brief   Used as a first-order check by all API functions that take a IotGpioHandle_t
 *
 * @param[in] pxGpio  IotGpioHandle_t to validate
 *
 * @return    true if handle is available for API interface
 *
 */
static bool prvIsValidHandle( IotGpioHandle_t const pxGpio )
{
    return ( pxGpio != NULL ) && ( pxGpio->ucState == IOT_GPIO_OPENED );
}

/*
 * @brief   Maps CommonIO Drive mode setting to corresponding HAL setting.
 *
 * @param[in] xOutMode      CommonIO setting to map to HAL setting
 * @param{in] xOutMode_NRF  Address to store board HAL specific value hat corresponds with xOutMode
 *
 * @return    true if the xOutMode is within range of valid HAL settings and could map
 *
 */
static int32_t prvGetOutModeForHAL( IotGpioOutputMode_t xOutMode,
                                    uint32_t * xOutMode_STM )
{
    int32_t lReturnCode = IOT_GPIO_SUCCESS;

    switch( xOutMode )
    {
        case eGpioOpenDrain:
            *xOutMode_STM = GPIO_MODE_OUTPUT_OD;
            break;

        case eGpioPushPull:
            *xOutMode_STM = GPIO_MODE_OUTPUT_PP;
            break;

        default:
            lReturnCode = IOT_GPIO_INVALID_VALUE;
    }

    return lReturnCode;
}

static int32_t prvGetSpeedForHAL( int32_t lSpeed,
                               uint32_t * ulSpeed_STM )
{
    int32_t lReturnCode = IOT_GPIO_SUCCESS;

    switch( lSpeed )
    {
        case GPIO_SPEED_FREQ_LOW:
        case GPIO_SPEED_FREQ_MEDIUM:
        case GPIO_SPEED_FREQ_HIGH:
        case GPIO_SPEED_FREQ_VERY_HIGH:
            *ulSpeed_STM = lSpeed;
            break;

        default:
            lReturnCode = IOT_GPIO_INVALID_VALUE;
    }

    return lReturnCode;
}

static int32_t prvGetPullForHAL( IotGpioPull_t xPull,
                              uint32_t * xPull_STM )
{
    int32_t lReturnCode = IOT_GPIO_SUCCESS;

    switch( xPull )
    {
        case eGpioPullNone:
            *xPull_STM = GPIO_NOPULL;
            break;

        case eGpioPullUp:
            *xPull_STM = GPIO_PULLUP;
            break;

        case eGpioPullDown:
            *xPull_STM = GPIO_PULLDOWN;
            break;

        default:
            lReturnCode = IOT_GPIO_INVALID_VALUE;
    }

    return lReturnCode;
}

static int32_t prvGetInterruptModeForHAL( IotGpioInterrupt_t xInterrupt,
                                          uint32_t * xInterruptMode_STM )
{
    int32_t lReturnCode = IOT_GPIO_SUCCESS;

    switch( xInterrupt )
    {
        case eGpioInterruptNone:
            break;

        case eGpioInterruptRising:
            *xInterruptMode_STM = GPIO_MODE_IT_RISING;
            break;

        case eGpioInterruptFalling:
            *xInterruptMode_STM = GPIO_MODE_IT_FALLING;
            break;

        case eGpioInterruptEdge:
            *xInterruptMode_STM = GPIO_MODE_IT_RISING_FALLING;
            break;

        case eGpioInterruptLow:
        case eGpioInterruptHigh:
            lReturnCode = IOT_GPIO_FUNCTION_NOT_SUPPORTED;
            IOT_GPIO_LOG( " Warning: This board does not support level-based interrupts.\r\n"
                          "        Similar behaviour can be implemented only using edge-based interrupts\r\n" );
            break;

        default:
            lReturnCode = IOT_GPIO_INVALID_VALUE;
            break;
    }

    return lReturnCode;
}

static int32_t prvGetFunctionForHAL( IotGpioInterrupt_t xFunction,
                                     uint32_t * xFunction_STM )
{
    int32_t lReturnCode = IOT_GPIO_SUCCESS;

    if( IS_GPIO_AF( (uint32_t)xFunction ) )
    {
    	*xFunction_STM = xFunction;
    }
    else
    {
    	lReturnCode = IOT_GPIO_INVALID_VALUE;
    }

	return lReturnCode;
}


static void prvConfigFromPinState( IotGpioDescriptor_t * pxDescriptor )
{

    IotGpioConfig_t * pxConfig = pxDescriptor->xConfig;
    IotMappedPin_t * pxPinMap = pxGpioMap[ pxDescriptor->lGpioNumber ];

    uint32_t pinNum = POSITION_VAL( pxPinMap->xPinMask );

    uint32_t reg32_mask = ( GPIO_PUPDR_PUPD0_Msk << ( 2U * pinNum ) );

    /* Determine direction */
    switch( ( pxPinMap->xPort->MODER & reg32_mask ) >> (2U * pinNum ) )
    {
    case LL_GPIO_MODE_INPUT:
        pxConfig->xDirection = eGpioDirectionInput;
        break;
    case LL_GPIO_MODE_OUTPUT:
        pxConfig->xDirection = eGpioDirectionOutput;
        break;
    case LL_GPIO_MODE_ALTERNATE:
    case LL_GPIO_MODE_ANALOG:
    default:
        pxConfig->xDirection = eGpioDirectionUnknown;
        break;
    }

    /* Determine output mode / function */
    switch( ( pxPinMap->xPort->MODER & reg32_mask ) >> (2U * pinNum ) )
    {
    case LL_GPIO_OUTPUT_PUSHPULL:
        pxConfig->xDirection = eGpioDirectionInput;
        break;
    case LL_GPIO_MODE_OUTPUT:
        pxConfig->xDirection = eGpioDirectionOutput;
        break;
    case LL_GPIO_MODE_ALTERNATE:
    case LL_GPIO_MODE_ANALOG:
    default:
        pxConfig->xDirection = eGpioDirectionUnknown;
        break;
    }

    /* Determine slew rate / speed */
    pxConfig->lSpeed = LL_GPIO_GetPinSpeed( pxPinMap->xPort, pxPinMap->xPinMask );

    /* Determine pull type */
    switch( LL_GPIO_GetPinPull( pxPinMap->xPort, pxPinMap->xPinMask ) )
    {
    case LL_GPIO_PULL_NO:
        pxConfig->xPull = eGpioPullNone;
        break;
    case LL_GPIO_PULL_UP:
        pxConfig->xPull = eGpioPullUp;
        break;
    case LL_GPIO_PULL_DOWN:
        pxConfig->xPull = eGpioPullDown;
        break;
    default:
        assert( pdFALSE );
        break;
    }
}


/*
 * @brief   This event handler gets installed for all gpio
 *

 *
 * @param[in] xPin STM pin index that had a event triggered.
 *
 */
static void prvPinEventHandler( uint16_t xPin_STM )
{
//    IotGpioHandle_t pxGpio = NULL;
//    IotMappedPin_t * pxMappedPin = NULL;
//
//    for( int i = 0; i < IOT_COMMON_IO_GPIO_NUMBER_OF_PINS; i++ )
//    {
//        if( pxGpioMap[ pxGpioDesc[ i ].lGpioNumber ].xPinMask == xPin_STM )
//        {
//            pxGpio = &pxGpioDesc[ i ];
//            pxMappedPin = &pxGpioMap[ pxGpioDesc[ i ].lGpioNumber ];
//            break;
//        }
//    }
//
//    if( ( pxGpio != NULL ) && ( pxGpio->xUserCallback != NULL ) && ( pxGpio->xConfig.xInterruptMode != eGpioInterruptNone ) )
//    {
//        pxGpio->xUserCallback( (uint8_t)HAL_GPIO_ReadPin( pxMappedPin->xPort, pxMappedPin->xPinMask ), pxGpio->pvUserContext );
//    }


    // This implementation is horribly broken.
}

static void prvEnablePortClock( IotGpioHandle_t const pxGpio )
{
	IotMappedPin_t * pxMappedPin = &pxGpioMap[ pxGpio->lGpioNumber ];
	switch( (uint32_t)pxMappedPin->xPort )
	{
		case (uint32_t)GPIOA:
			__HAL_RCC_GPIOA_CLK_ENABLE();
			break;

		case (uint32_t)GPIOB:
			__HAL_RCC_GPIOB_CLK_ENABLE();
			break;

		case (uint32_t)GPIOC:
			__HAL_RCC_GPIOC_CLK_ENABLE();
			break;

		case (uint32_t)GPIOD:
			__HAL_RCC_GPIOD_CLK_ENABLE();
			break;

		case (uint32_t)GPIOE:
			__HAL_RCC_GPIOE_CLK_ENABLE();
			break;

		case (uint32_t)GPIOF:
			__HAL_RCC_GPIOF_CLK_ENABLE();
			break;

		case (uint32_t)GPIOG:
			__HAL_RCC_GPIOG_CLK_ENABLE();
			break;

		case (uint32_t)GPIOH:
			__HAL_RCC_GPIOH_CLK_ENABLE();
			break;

		case (uint32_t)GPIOI:
			__HAL_RCC_GPIOI_CLK_ENABLE();
			break;

		default:
			break;
	}
}

static void prvEnablePinInterrupt( IotGpioHandle_t const pxGpio )
{
	IotMappedPin_t * pxMappedPin = &pxGpioMap[ pxGpio->lGpioNumber ];

	assert( PIN_VALUE( pxMappedPin->xPinMask ) < 16 );

	IRQn_Type xIRQn = EXTI0_IRQn + PIN_VALUE( pxMappedPin->xPinMask );

	assert( xIRQn >= EXTI0_IRQn );
	assert( xIRQn >= EXTI15_IRQn );

	HAL_NVIC_SetPriority(xIRQn, IOT_GPIO_INTERRUPT_PRIORITY, 0);
	HAL_NVIC_EnableIRQ(xIRQn);
}

static void prvDisablePinInterrupt( IotGpioHandle_t const pxGpio )
{
    /* Skip this because interrupts are shared between pins with the same id */
//	IotMappedPin_t * pxMappedPin = &pxGpioMap[ pxGpio->lGpioNumber ];
//	switch( pxMappedPin->xPinMask )
//	{
//		case GPIO_PIN_0:
//		    HAL_NVIC_DisableIRQ(EXTI0_IRQn);
//		case GPIO_PIN_1:
//		    HAL_NVIC_DisableIRQ(EXTI1_IRQn);
//		case GPIO_PIN_2:
//		    HAL_NVIC_DisableIRQ(EXTI2_IRQn);
//		case GPIO_PIN_3:
//		    HAL_NVIC_DisableIRQ(EXTI3_IRQn);
//		case GPIO_PIN_4:
//		    HAL_NVIC_DisableIRQ(EXTI4_IRQn);
//		case GPIO_PIN_5:
//		    HAL_NVIC_DisableIRQ(EXTI5_IRQn);
//		case GPIO_PIN_6:
//		    HAL_NVIC_DisableIRQ(EXTI6_IRQn);
//		case GPIO_PIN_7:
//		    HAL_NVIC_DisableIRQ(EXTI7_IRQn);
//		case GPIO_PIN_8:
//		    HAL_NVIC_DisableIRQ(EXTI8_IRQn);
//		case GPIO_PIN_9:
//		    HAL_NVIC_DisableIRQ(EXTI9_IRQn);
//		case GPIO_PIN_10:
//		    HAL_NVIC_DisableIRQ(EXTI10_IRQn);
//		case GPIO_PIN_11:
//		    HAL_NVIC_DisableIRQ(EXTI11_IRQn);
//		case GPIO_PIN_12:
//		    HAL_NVIC_DisableIRQ(EXTI12_IRQn);
//		case GPIO_PIN_13:
//		    HAL_NVIC_DisableIRQ(EXTI13_IRQn);
//		case GPIO_PIN_14:
//		    HAL_NVIC_DisableIRQ(EXTI14_IRQn);
//		case GPIO_PIN_15:
//		    HAL_NVIC_DisableIRQ(EXTI15_IRQn);
//	}
}

/*
 * @brief   Configures a board pin, mapping CommonIO settings to board HAL settings.
 *          Assumes higher-level API verify parameters before making this call.
 *
 * @param[in] pxGpio  Handle with settings to apply to input pin
 * @param[in] pxNewConfig  Tentative new settings for pxGpio
 *
 */
static int32_t prvConfigurePin( IotGpioHandle_t const pxGpio,
                                const IotGpioConfig_t * const pxNewConfig )
{
    int32_t lReturnCode = IOT_GPIO_SUCCESS;

    /* Validate and map all CommonIO settings to STM HAL settings */
    uint32_t ulPull_STM;
    lReturnCode = prvGetPullForHAL( pxNewConfig->xPull, &ulPull_STM );

    uint32_t ulInterruptMode_STM;
    if( lReturnCode == IOT_GPIO_SUCCESS )
    {
        lReturnCode = prvGetInterruptModeForHAL( pxNewConfig->xInterruptMode, &ulInterruptMode_STM );
    }

    uint32_t ulSpeed_STM;
    if( lReturnCode == IOT_GPIO_SUCCESS )
    {
    	lReturnCode = prvGetSpeedForHAL( pxNewConfig->lSpeed, &ulSpeed_STM );
    }

    uint32_t ulOutMode_STM;
    if( lReturnCode == IOT_GPIO_SUCCESS )
    {
    	lReturnCode = prvGetOutModeForHAL( pxNewConfig->xOutMode, &ulOutMode_STM );
    }

    uint32_t ulFunction_STM;
    if( lReturnCode == IOT_GPIO_SUCCESS )
    {
    	lReturnCode = prvGetFunctionForHAL( pxNewConfig->lFunction, &ulFunction_STM );
    }

    /* Set pin configuration */
    if( lReturnCode == IOT_GPIO_SUCCESS )
    {
        GPIO_InitTypeDef GPIO_InitStruct;
    	IotMappedPin_t * pxMappedPin = &pxGpioMap[ pxGpio->lGpioNumber ];

    	GPIO_InitStruct.Pin = pxMappedPin->xPinMask;
    	GPIO_InitStruct.Pull = ulPull_STM;
    	GPIO_InitStruct.Speed = ulSpeed_STM;
    	GPIO_InitStruct.Alternate = ulFunction_STM;

    	HAL_GPIO_DeInit( pxMappedPin->xPort, pxMappedPin->xPinMask );
        prvDisablePinInterrupt( pxGpio );
        if( pxNewConfig->xDirection == eGpioDirectionInput )
        {
        	if( pxNewConfig->xInterruptMode != eGpioInterruptNone )
        	{
        		GPIO_InitStruct.Mode = ulInterruptMode_STM;
            	HAL_GPIO_Init( pxMappedPin->xPort, &GPIO_InitStruct );
            	prvEnablePinInterrupt( pxGpio );
        	}
        	else
        	{
        		GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
            	HAL_GPIO_Init( pxMappedPin->xPort, &GPIO_InitStruct );
        	}
        }
        else
        {
    		GPIO_InitStruct.Mode = ulOutMode_STM;
        	HAL_GPIO_Init( pxMappedPin->xPort, &GPIO_InitStruct );
        }
    }


    if( lReturnCode == IOT_GPIO_SUCCESS )
    {
        /* Update was successful, update descriptor to reflect new settings */
        pxGpio->xConfig = *pxNewConfig;
    }

    return lReturnCode;
}

/*---------------------------------------------------------------------------------*
*                               API Implementation                                *
*---------------------------------------------------------------------------------*/
void iot_gpio_init( const IotMappedPin_t * const pxGpioMa,
                    const IotGpioDescriptor_t * pxGpioDesc )
{

}

IotGpioHandle_t iot_gpio_open( int32_t lGpioNumber )
{
    IotGpioHandle_t xReturnHandle = NULL;

    if( prvIsValidPinIndex( lGpioNumber ) )
    {
        IotGpioHandle_t pxGpio = &pxGpioDesc[ lGpioNumber ];

        if( pxGpio->ucState == IOT_GPIO_CLOSED )
        {
            /* Initialize since descriptor VLA starts as uninitialized */

            pxGpio->lGpioNumber = lGpioNumber;

            if( IOT_GPIO_SUCCESS == prvConfigurePin( pxGpio, &xDefaultGpioDesc.xConfig ) )
            {
            	/* Enable port clocks. Pins share port clocks, so we don't disable upon individual pin closure */
            	prvEnablePortClock( pxGpio );

                /* Claim descriptor */
                pxGpio->ucState = IOT_GPIO_OPENED;
                xReturnHandle = pxGpio;
            }
        }
        else
        {
            IOT_GPIO_LOGF( " Cannot open. GPIO[%d] is already opened\r\n", lGpioNumber );
        }
    }
    else
    {
        IOT_GPIO_LOGF( " Incorrect pin index[%d]. Please verify IOT_COMMON_IO_GPIO_PIN_MAP\r\n", lGpioNumber );
    }

    return xReturnHandle;
}

void iot_gpio_set_callback( IotGpioHandle_t const pxGpio,
                            IotGpioCallback_t xGpioCallback,
                            void * pvUserContext )
{
    if( prvIsValidHandle( pxGpio ) && ( xGpioCallback != NULL ) )
    {
        pxGpio->xUserCallback = xGpioCallback;
        pxGpio->pvUserContext = pvUserContext;
    }
}


int32_t iot_gpio_read_sync( IotGpioHandle_t const pxGpio,
                            uint8_t * pucPinState )
{
    int32_t lReturnCode = IOT_GPIO_SUCCESS;

    if( prvIsValidHandle( pxGpio ) && ( pxGpio->xConfig.xDirection == eGpioDirectionInput ) )
    {
    	IotMappedPin_t * pxMappedPin = &pxGpioMap[ pxGpio->lGpioNumber ];
        *pucPinState = (uint8_t)HAL_GPIO_ReadPin( pxMappedPin->xPort, pxMappedPin->xPinMask );
    }
    else
    {
        lReturnCode = IOT_GPIO_INVALID_VALUE;
    }

    return lReturnCode;
}


int32_t iot_gpio_write_sync( IotGpioHandle_t const pxGpio,
                             uint8_t ucPinState )
{
    int lReturnCode = IOT_GPIO_SUCCESS;

    if( prvIsValidHandle( pxGpio ) && ( pxGpio->xConfig.xDirection == eGpioDirectionOutput ) )
    {
    	IotMappedPin_t * pxMappedPin = &pxGpioMap[ pxGpio->lGpioNumber ];
    	HAL_GPIO_WritePin( pxMappedPin->xPort, pxMappedPin->xPinMask, (GPIO_PinState)ucPinState );
    }
    else
    {
        lReturnCode = IOT_GPIO_INVALID_VALUE;
    }

    return lReturnCode;
}

int32_t iot_gpio_close( IotGpioHandle_t const pxGpio )
{
    int32_t lReturnCode = IOT_GPIO_SUCCESS;

    if( prvIsValidHandle( pxGpio ) )
    {
        /* Restore back to default settings. */
    	IotMappedPin_t * pxMappedPin = &pxGpioMap[ pxGpio->lGpioNumber ];

        pxGpio->ucState = IOT_GPIO_CLOSED;
//        prvDisablePinInterrupt( pxGpio );
//        HAL_GPIO_DeInit( pxMappedPin->xPort, pxMappedPin->xPinMask );
        pxGpio->xUserCallback = NULL;
        pxGpio->pvUserContext = NULL;
    }
    else
    {
        lReturnCode = IOT_GPIO_INVALID_VALUE;
    }

    return lReturnCode;
}

int32_t iot_gpio_ioctl( IotGpioHandle_t const pxGpio,
                        IotGpioIoctlRequest_t xRequest,
                        void * const pvBuffer )
{
    int32_t lReturnCode = IOT_GPIO_INVALID_VALUE;

    if( prvIsValidHandle( pxGpio ) )
    {
        IotGpioConfig_t xNewConfig = pxGpio->xConfig;
        lReturnCode = IOT_GPIO_SUCCESS;

        switch( xRequest )
        {
            case eSetGpioDirection:
                memcpy( &xNewConfig.xDirection, pvBuffer, sizeof( xNewConfig.xDirection ) );
                lReturnCode = prvConfigurePin( pxGpio, &xNewConfig );
                break;

            case eGetGpioDirection:
                memcpy( pvBuffer, &pxGpio->xConfig.xDirection, sizeof( pxGpio->xConfig.xDirection ) );
                break;

            case eSetGpioPull:
                memcpy( &xNewConfig.xPull, pvBuffer, sizeof( xNewConfig.xPull ) );
                lReturnCode = prvConfigurePin( pxGpio, &xNewConfig );
                break;

            case eGetGpioPull:
                memcpy( pvBuffer, &pxGpio->xConfig.xPull, sizeof( pxGpio->xConfig.xPull ) );
                break;

            case eSetGpioOutputMode:
                memcpy( &xNewConfig.xOutMode, pvBuffer, sizeof( xNewConfig.xOutMode ) );
                lReturnCode = prvConfigurePin( pxGpio, &xNewConfig );
                break;

            case eGetGpioOutputType:
                memcpy( pvBuffer, &pxGpio->xConfig.xOutMode, sizeof( pxGpio->xConfig.xOutMode ) );
                break;

            case eSetGpioInterrupt:
                memcpy( &xNewConfig.xInterruptMode, pvBuffer, sizeof( xNewConfig.xInterruptMode ) );
                lReturnCode = prvConfigurePin( pxGpio, &xNewConfig );
                /* TODO: Add to interrupt_to_gpio_map */
                break;

            case eGetGpioInterrupt:
                memcpy( pvBuffer, &pxGpio->xConfig.xInterruptMode, sizeof( pxGpio->xConfig.xInterruptMode ) );
                break;

            case eSetGpioSpeed:
                memcpy( &xNewConfig.lSpeed, pvBuffer, sizeof( xNewConfig.lSpeed ) );
                lReturnCode = prvConfigurePin( pxGpio, &xNewConfig );
                break;

            case eGetGpioSpeed:
                memcpy( pvBuffer, &pxGpio->xConfig.lSpeed, sizeof( pxGpio->xConfig.lSpeed ) );
                break;

            case eSetGpioFunction:
                memcpy( &xNewConfig.lFunction, pvBuffer, sizeof( xNewConfig.lFunction ) );
                lReturnCode = prvConfigurePin( pxGpio, &xNewConfig );
                break;

            case eGetGpioFunction:
                memcpy( pvBuffer, &pxGpio->xConfig.lFunction, sizeof( pxGpio->xConfig.lFunction ) );
                break;

            /* Unsupported functions */
            case eSetGpioDriveStrength:
            case eGetGpioDriveStrength:
            default:
                lReturnCode = IOT_GPIO_FUNCTION_NOT_SUPPORTED;
                IOT_GPIO_LOGF( " Warning: ioctl[%d] is unsupported and was ignored\r\n", xRequest );
                break;
        }
    }

    return lReturnCode;
}

void HAL_GPIO_EXTI_Rising_Callback( uint16_t xPinMask )
{
	prvPinEventHandler( xPinMask );
}

void HAL_GPIO_EXTI_Falling_Callback( uint16_t xPinMask )
{
	prvPinEventHandler( xPinMask );
}

void EXTI0_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void EXTI1_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
}

void EXTI2_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2);
}

void EXTI3_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
}

void EXTI4_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
}

void EXTI5_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
}

void EXTI6_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
}

void EXTI7_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);
}

void EXTI8_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
}

void EXTI9_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);
}

void EXTI10_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
}

void EXTI11_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);
}

void EXTI12_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
}

void EXTI13_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
}

void EXTI14_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);
}

void EXTI15_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
}
