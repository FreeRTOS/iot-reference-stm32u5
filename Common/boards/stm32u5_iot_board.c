/* STM32u5 Common-IO board file */
#include "iot_gpio_stm32_prv.h"
#include "stm32u5_iot_board.h"

const IotMappedPin_t xGpioMap[ GPIO_MAX ] =
{
    { GPIOG, GPIO_PIN_15, EXTI15_IRQn }, /* GPIO_MX_FLOW     */
    { GPIOF, GPIO_PIN_15, 0           }, /* GPIO_MX_RESET    */
    { GPIOB, GPIO_PIN_12, 0           }, /* GPIO_MX_NSS      */
    { GPIOD, GPIO_PIN_14, EXTI14_IRQn }, /* GPIO_MX_NOTIFY   */
    { GPIOH, GPIO_PIN_6,  0           }, /* GPIO_LED_RED     */
    { GPIOH, GPIO_PIN_7,  0           }  /* GPIO_LED_GREEN   */
};

/*const IotGpioDescriptor_t xGpioDesc[ GPIO_MAX ] = { */
/*        { GPIO_MX_FLOW,     { eGpioDirectionInput,  eGpioPushPull,  eGpioPullNone, eGpioInterruptNone, 0 }, NULL, NULL, IOT_GPIO_CLOSED }, */
/*        { GPIO_MX_RESET,    { eGpioDirectionInput,  eGpioPushPull,  eGpioPullNone, eGpioInterruptNone, 0 }, NULL, NULL, IOT_GPIO_CLOSED }, */
/*        { GPIO_MX_NSS,      { eGpioDirectionInput,  eGpioPushPull,  eGpioPullNone, eGpioInterruptNone, 0 }, NULL, NULL, IOT_GPIO_CLOSED }, */
/*        { GPIO_MX_NOTIFY,   { eGpioDirectionInput,  eGpioPushPull,  eGpioPullNone, eGpioInterruptNone, 0 }, NULL, NULL, IOT_GPIO_CLOSED }, */
/*        { GPIO_LED_RED,     { eGpioDirectionOutput, eGpioOpenDrain, eGpioPullNone, eGpioInterruptNone, 0 }, NULL, NULL, IOT_GPIO_CLOSED }, */
/*        { GPIO_LED_GREEN,   { eGpioDirectionOutput, eGpioOpenDrain, eGpioPullNone, eGpioInterruptNone, 0 }, NULL, NULL, IOT_GPIO_CLOSED } */
/*}; */
