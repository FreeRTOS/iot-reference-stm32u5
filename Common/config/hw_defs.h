#ifndef __HW_DEFS
#define __HW_DEFS

#include "stm32u5xx_hal.h"

#define LED_RED_Pin                GPIO_PIN_6
#define LED_RED_GPIO_Port          GPIOH

#define LED_GREEN_Pin              GPIO_PIN_7
#define LED_GREEN_GPIO_Port        GPIOH

#define MXCHIP_FLOW_Pin            GPIO_PIN_15
#define MXCHIP_FLOW_GPIO_Port      GPIOG
#define MXCHIP_FLOW_EXTI_IRQn      EXTI15_IRQn

#define MXCHIP_NOTIFY_Pin          GPIO_PIN_14
#define MXCHIP_NOTIFY_GPIO_Port    GPIOD
#define MXCHIP_NOTIFY_EXTI_IRQn    EXTI14_IRQn

#define MXCHIP_NSS_Pin             GPIO_PIN_12
#define MXCHIP_NSS_GPIO_Port       GPIOB

#define MXCHIP_RESET_Pin           GPIO_PIN_15
#define MXCHIP_RESET_GPIO_Port     GPIOF

extern RTC_HandleTypeDef * pxHndlRtc;
extern SPI_HandleTypeDef * pxHndlSpi2;
extern TIM_HandleTypeDef * pxHndlTim5;
extern UART_HandleTypeDef * pxHndlUart1;
extern DCACHE_HandleTypeDef * pxHndlDCache;
extern DMA_HandleTypeDef * pxHndlGpdmaCh4;
extern DMA_HandleTypeDef * pxHndlGpdmaCh5;
extern IWDG_HandleTypeDef * pxHwndIwdg;

static inline uint32_t timer_get_count( TIM_HandleTypeDef * pxHndl )
{
    if( pxHndl )
    {
        return __HAL_TIM_GetCounter( pxHndlTim5 );
    }
    else
    {
        return 0;
    }
}

void hw_init( void );

typedef void ( * GPIOInterruptCallback_t ) ( void * pvContext );

void GPIO_EXTI_Register_Callback( uint16_t usGpioPinMask,
                                  GPIOInterruptCallback_t pvCallback,
                                  void * pvContext );

void vDoSystemReset( void );

static inline void vPetWatchdog( void )
{
    /* Check / pet the watchdog */
    if( pxHwndIwdg != NULL )
    {
        HAL_IWDG_Refresh( pxHwndIwdg );
    }
}


#endif /* __HW_DEFS */
