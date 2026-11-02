/* The chip, for the host test only (tools/boottest.c, with the real
 * include/board.h): the registers main.c and uart.c touch, as plain memory.
 * Every access to a peripheral goes through chip_access(), which lets the
 * test's clock run: bytes arrive on the wire, the UART sends at 115200, and
 * SysTick and the USART interrupt fire. Reading RDR is what takes a byte off
 * the receiver, so that read is a call too. */
#ifndef CHIP_STUB_H
#define CHIP_STUB_H

#include <stdint.h>

typedef struct { volatile uint32_t AHB2ENR, APB2ENR; } RCC_TypeDef;
typedef struct { volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, AFR[2]; } GPIO_TypeDef;
typedef struct { volatile uint32_t CR1, CR2, CR3, BRR, ISR, ICR, TDR; uint32_t (*rdr)(void); } USART_TypeDef;
typedef struct { volatile uint32_t CTRL, LOAD, VAL; } SysTick_Type;

#define RDR rdr()                       /* VCP_UART->RDR calls the receiver */

extern RCC_TypeDef   chip_rcc;
extern GPIO_TypeDef  chip_gpio[4];
extern USART_TypeDef chip_usart1;
extern SysTick_Type  chip_systick;
void *chip_access(void *peripheral);

#define RCC     ((RCC_TypeDef *)chip_access(&chip_rcc))
#define GPIOA   ((GPIO_TypeDef *)chip_access(&chip_gpio[0]))
#define GPIOB   ((GPIO_TypeDef *)chip_access(&chip_gpio[1]))
#define GPIOC   ((GPIO_TypeDef *)chip_access(&chip_gpio[2]))
#define GPIOD   ((GPIO_TypeDef *)chip_access(&chip_gpio[3]))
#define USART1  ((USART_TypeDef *)chip_access(&chip_usart1))
#define SysTick ((SysTick_Type *)chip_access(&chip_systick))

#define RCC_AHB2ENR_GPIOAEN  (1u << 0)
#define RCC_AHB2ENR_GPIOBEN  (1u << 1)
#define RCC_AHB2ENR_GPIOCEN  (1u << 2)
#define RCC_AHB2ENR_GPIODEN  (1u << 3)
#define RCC_APB2ENR_USART1EN (1u << 14)

/* The bits where include/st/stm32wba65xx.h places them. */
#define USART_CR1_UE     (1u << 0)
#define USART_CR1_RE     (1u << 2)
#define USART_CR1_TE     (1u << 3)
#define USART_CR1_RXNEIE (1u << 5)
#define USART_ISR_PE     (1u << 0)
#define USART_ISR_FE     (1u << 1)
#define USART_ISR_NE     (1u << 2)
#define USART_ISR_ORE    (1u << 3)
#define USART_ISR_RXNE   (1u << 5)
#define USART_ISR_TC     (1u << 6)
#define USART_ISR_TXE    (1u << 7)
#define USART_ICR_PECF   (1u << 0)
#define USART_ICR_FECF   (1u << 1)
#define USART_ICR_NECF   (1u << 2)
#define USART_ICR_ORECF  (1u << 3)

#define SysTick_CTRL_ENABLE_Msk    (1u << 0)
#define SysTick_CTRL_TICKINT_Msk   (1u << 1)
#define SysTick_CTRL_CLKSOURCE_Msk (1u << 2)

/* The one interrupt this firmware enables is USART1's: enabling it hands the
 * test its handler. */
typedef enum { USART1_IRQn = 46 } IRQn_Type;
void USART1_IRQHandler(void);
void chip_enable_irq(IRQn_Type irq, void (*handler)(void));
void NVIC_SetPriority(IRQn_Type irq, uint32_t priority);
#define NVIC_EnableIRQ(irq) chip_enable_irq((irq), USART1_IRQHandler)

extern unsigned test_uid;               /* the die's unique id, from the test */
#define UID_BASE             ((uintptr_t)&test_uid)
#define SYSTEM_FLASH_BASE_NS 0x0BF90000u

#endif /* CHIP_STUB_H */
