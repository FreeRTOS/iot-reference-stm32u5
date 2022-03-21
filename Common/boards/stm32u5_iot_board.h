/* STM32u5 Common-IO board file */

#ifndef _COMMON_IO_BOAD_U5_
#define _COMMON_IO_BOAD_U5_

#include "stm32u585xx.h"
#include "stm32u5xx_hal.h"

#include "iot_gpio_stm32_prv.h"

typedef enum GpioPin
{
    GPIO_MX_FLOW,
    GPIO_MX_RESET,
    GPIO_MX_NSS,
    GPIO_MX_NOTIFY,
    GPIO_LED_RED,
    GPIO_LED_GREEN,
    GPIO_MAX
} GpioPin_t;

extern const IotMappedPin_t xGpioMap[ GPIO_MAX ];

/*extern const IotGpioDescriptor_t xGpioDesc[ GPIO_MAX ]; */

#endif /* _COMMON_IO_BOAD_U5_ */
