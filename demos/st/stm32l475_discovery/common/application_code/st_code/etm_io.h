/**
  ******************************************************************************
  * @file    etm_io.h
  * @author  Paul Tupper @ Eseye
  ******************************************************************************
  */

#ifndef __ETM_IO__
#define __ETM_IO__

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Exported constants --------------------------------------------------------*/
/* This section can be used to tailor UART_C2C instance used and associated
   resources */
#define UART_C2C                           UART4
#define UART_C2C_CLK_ENABLE()              __HAL_RCC_UART4_CLK_ENABLE();
#define DMAx_CLK_ENABLE()                   __HAL_RCC_DMA2_CLK_ENABLE()
#define UART_C2C_RX_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOA_CLK_ENABLE()
#define UART_C2C_TX_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOA_CLK_ENABLE()

#define UART_C2C_FORCE_RESET()             __HAL_RCC_UART4_FORCE_RESET()
#define UART_C2C_RELEASE_RESET()           __HAL_RCC_UART4_RELEASE_RESET()

/* Definition for UART_C2C Pins */
#define UART_C2C_TX_PIN                    GPIO_PIN_0
#define UART_C2C_TX_GPIO_PORT              GPIOA
#define UART_C2C_TX_AF                     GPIO_AF8_UART4


/* PortG on L696AG device needs Independent I/O supply rail;
   It can be enabled by setting the IOSV bit in the PWR_CR2 register, 
   when the VDDIO2 supply is present (depends by the package).*/
#define UART_C2C_RX_PIN                    GPIO_PIN_1
#define UART_C2C_RX_GPIO_PORT              GPIOA
#define UART_C2C_RX_AF                     GPIO_AF8_UART4

#define UART_C2C_RTS_PIN                   GPIO_PIN_2
#define UART_C2C_RTS_GPIO_PORT             GPIOA
//#define UART_C2C_RTS_AF                    GPIO_AF7_USART1

#define UART_C2C_CTS_PIN                   GPIO_PIN_14
#define UART_C2C_CTS_GPIO_PORT             GPIOD
//#define UART_C2C_CTS_AF                    GPIO_AF7_USART1
   
/* Definition for UART_C2C's NVIC IRQ and IRQ Handlers */
#define UART_C2C_IRQn                      UART4_IRQn
//#define UART_C2C_IRQHandler                USART1_IRQHandler

/* Definition for UART_C2C's DMA */
#define UART_C2C_TX_DMA_CHANNEL            DMA2_Channel3
/* Definition for UART_C2C's DMA Request */
#define UART_C2C_TX_DMA_REQUEST            DMA_REQUEST_2
/* Definition for UART_C2C's NVIC */
#define UART_C2C_DMA_TX_IRQn               DMA2_Channel3_IRQn
//#define UART_C2C_DMA_TX_IRQHandler         DMA1_Channel4_IRQHandler

/* C2C module Reset pin definitions */
//#define C2C_RST_PIN                        GPIO_PIN_2
//#define C2C_RST_GPIO_PORT                  GPIOC
//#define C2C_RST_GPIO_CLK_ENABLE()          __HAL_RCC_GPIOC_CLK_ENABLE()

/* C2C module PowerKey pin definitions */
#define C2C_PWRKEY_PIN                     GPIO_PIN_2
#define C2C_PWRKEY_GPIO_PORT               GPIOC
#define C2C_PWRKEY_GPIO_CLK_ENABLE()       __HAL_RCC_GPIOC_CLK_ENABLE()

/* Modem status pin */
#define C2C_STATUS_PIN                GPIO_PIN_5
#define C2C_STATUS_GPIO_PORT          GPIOC
#define C2C_STATUS_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOC_CLK_ENABLE()

int8_t  UART_C2C_Init(void);
int8_t  UART_C2C_DeInit(void);
int8_t  UART_C2C_SetBaudrate(uint32_t BaudRate);
void    UART_C2C_FlushBuffer(void);
int16_t UART_C2C_SendData(uint8_t* Buffer, uint16_t Length);
int16_t UART_C2C_ReceiveSingleData(uint8_t* pData);

#ifdef __cplusplus
}
#endif

#endif /* __ETM_IO__ */

