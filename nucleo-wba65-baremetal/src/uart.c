/* USART1 on the ST-LINK VCP pins, 115200 8N1. Sending is polled. Receiving
 * is done by the interrupt, into a ring the main loop drains, so bytes keep
 * coming in while the main loop prints: a report takes about 40 ms on the
 * wire, and the receiver itself holds one byte (87 us). */

#include "uart.h"
#include "board.h"

/* Interrupt-to-main-loop ring, 1023 bytes: 89 ms of bytes at full speed, more
 * than twice the longest report, and over 15 pieces of a push as the setup
 * page paces it (64 bytes, then 30 ms). Each entry is a byte and its flags.
 * A flash erase or write masks interrupts, so the ring takes nothing while
 * one runs; a store runs only between a line's ending and its answer, when a
 * host keeping the contract is waiting and sends nothing, and a byte that did
 * come then is an overrun, which marks its line damaged. */
#define RX_RING 1024u

static volatile unsigned short s_ring[RX_RING];
static volatile unsigned       s_head;     /* written by the interrupt only */
static volatile unsigned       s_tail;     /* written by uart_getc() only */
static unsigned                s_flags;    /* interrupt only: flags for the next byte kept */
static unsigned                s_last_ms;  /* interrupt only: when the last byte came */

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

  s_head = s_tail = 0u;
  s_flags = 0u;
  s_last_ms = millis();
  VCP_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
  NVIC_SetPriority(USART1_IRQn, 0u);
  NVIC_EnableIRQ(USART1_IRQn);
}

/* A byte came, or the receiver flagged trouble. A damaged byte (framing,
 * noise or parity error) is not kept, and an overrun means bytes after the
 * one in RDR were lost: either way the next byte kept carries UART_RX_LOST,
 * as does the next after one the full ring had no room for. A loss before a
 * pause belongs to the line the pause gives up, so a pause clears it. */
void USART1_IRQHandler(void)
{
  unsigned isr = VCP_UART->ISR;
  unsigned bad = USART_ISR_FE | USART_ISR_NE | USART_ISR_PE;

  if (isr & USART_ISR_RXNE) {
    unsigned short c   = (unsigned short)(VCP_UART->RDR & 0xFFu);   /* reading it clears RXNE */
    unsigned       now = millis();
    unsigned       next = (s_head + 1u == RX_RING) ? 0u : s_head + 1u;

    if (now - s_last_ms >= UART_IDLE_MS) { s_flags = UART_RX_PAUSED; }
    s_last_ms = now;

    if ((isr & bad) == 0u && next != s_tail) {
      s_ring[s_head] = (unsigned short)(c | s_flags);
      s_head  = next;
      s_flags = 0u;
    } else {
      s_flags |= UART_RX_LOST;
    }
  }
  if (isr & (bad | USART_ISR_ORE)) {
    VCP_UART->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    if (isr & USART_ISR_ORE) { s_flags |= UART_RX_LOST; }
  }
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

void uart_put_uint(unsigned v)
{
  char b[12];
  int i = 0;
  if (v == 0u) { uart_putc('0'); return; }
  while (v && i < 12) { b[i++] = (char)('0' + (v % 10u)); v /= 10u; }
  while (i--) { uart_putc(b[i]); }
}

void uart_put_hex(unsigned v, int digits)
{
  static const char H[] = "0123456789abcdef";
  for (int i = digits - 1; i >= 0; i--) { uart_putc(H[(v >> (i * 4)) & 0xFu]); }
}

int uart_getc(void)
{
  unsigned t = s_tail;
  int      e;

  if (t == s_head) { return -1; }
  e = s_ring[t];
  s_tail = (t + 1u == RX_RING) ? 0u : t + 1u;
  return e;
}

void uart_flush(void)
{
  while ((VCP_UART->ISR & USART_ISR_TC) == 0u) { }
}
