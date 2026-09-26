/* NUCLEO-WBA65RI pins from UM3448.
 * LD1: PD8, active low. B1: PC13, active low with pull-up.
 * ST-LINK VCP: USART1, PB12 TX and PA8 RX, both AF7. */
#ifndef BOARD_H
#define BOARD_H

#include "stm32wba65xx.h"

/* ---- LED (heartbeat) ---- */
#define LED_PORT        GPIOD
#define LED_PIN         8u
#define LED_RCC_EN      RCC_AHB2ENR_GPIODEN
#define LED_ACTIVE_LOW  1

/* ---- User button B1 ---- */
#define BTN_PORT        GPIOC
#define BTN_PIN         13u
#define BTN_RCC_EN      RCC_AHB2ENR_GPIOCEN
#define BTN_ACTIVE_LOW  1

/* ---- ST-LINK VCP ---- */
#define VCP_UART        USART1
#define VCP_RCC_EN      RCC_APB2ENR_USART1EN
#define VCP_TX_PORT     GPIOB
#define VCP_TX_PIN      12u
#define VCP_TX_AF       7u
#define VCP_TX_RCC_EN   RCC_AHB2ENR_GPIOBEN
#define VCP_RX_PORT     GPIOA
#define VCP_RX_PIN      8u
#define VCP_RX_AF       7u
#define VCP_RX_RCC_EN   RCC_AHB2ENR_GPIOAEN

/* Reset clock tree: HSI16 = SYSCLK = HCLK = PCLK2 = 16 MHz (ST system_stm32wbaxx.c).
 * We never touch RCC prescalers or CCIPR1, so USART1's kernel clock stays PCLK2. */
#define SYSCLK_HZ       16000000u
#define VCP_BAUD        115200u

/* System memory (ROM bootloader) — stm32wba65xx.h: SYSTEM_FLASH_BASE_NS */
#define SYSMEM_BASE     SYSTEM_FLASH_BASE_NS   /* 0x0BF90000 */

#endif /* BOARD_H */
