/* USART1 on the ST-LINK VCP pins, 115200 8N1, polled TX and RX. Polling in the
 * main loop cannot lose a byte the way an unprioritised RXNE interrupt can. */

#include "uart.h"
#include "board.h"

static void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af, uint32_t pull)
{
  port->MODER   = (port->MODER   & ~(3u << (pin * 2u))) | (2u << (pin * 2u)); /* AF */
  port->OTYPER &= ~(1u << pin);                                               /* push-pull */
  port->OSPEEDR = (port->OSPEEDR & ~(3u << (pin * 2u))) | (2u << (pin * 2u)); /* high speed */
  port->PUPDR   = (port->PUPDR   & ~(3u << (pin * 2u))) | (pull << (pin * 2u));
  if (pin < 8u) {
    port->AFR[0] = (port->AFR[0] & ~(0xFu << (pin * 4u))) | (af << (pin * 4u));
  } else {
    port->AFR[1] = (port->AFR[1] & ~(0xFu << ((pin - 8u) * 4u))) | (af << ((pin - 8u) * 4u));
  }
}

void uart_init(void)
{
  RCC->AHB2ENR |= VCP_TX_RCC_EN | VCP_RX_RCC_EN;
  RCC->APB2ENR |= VCP_RCC_EN;
  (void)RCC->APB2ENR;

  gpio_af(VCP_TX_PORT, VCP_TX_PIN, VCP_TX_AF, 0u);  /* TX, no pull */
  gpio_af(VCP_RX_PORT, VCP_RX_PIN, VCP_RX_AF, 1u);  /* RX, pull-up (idle high) */

  VCP_UART->CR1 = 0u;
  VCP_UART->CR2 = 0u;
  VCP_UART->CR3 = 0u;
  /* Kernel clock is PCLK2 = 16 MHz (reset tree, CCIPR1 untouched), OVER8 = 0. */
  VCP_UART->BRR = (SYSCLK_HZ + (VCP_BAUD / 2u)) / VCP_BAUD;   /* 139 @ 115200 */
  VCP_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void uart_putc(char c)
{
  while ((VCP_UART->ISR & USART_ISR_TXE) == 0u) { }
  VCP_UART->TDR = (uint8_t)c;
}

void uart_write(const char *s, unsigned n)
{
  for (unsigned i = 0; i < n; i++) { uart_putc(s[i]); }
}

void uart_puts(const char *s)
{
  while (*s) { uart_putc(*s++); }
}

void uart_line(const char *s)
{
  uart_puts(s);
  uart_putc('\r');
  uart_putc('\n');
}

int uart_getc(void)
{
  uint32_t isr = VCP_UART->ISR;

  /* Clear the sticky error flags; a framing/overrun glitch must not wedge RX. */
  if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
    VCP_UART->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
  }
  if ((isr & USART_ISR_RXNE) == 0u) { return -1; }
  return (int)(VCP_UART->RDR & 0xFFu);
}

void uart_flush(void)
{
  while ((VCP_UART->ISR & USART_ISR_TC) == 0u) { }
}
