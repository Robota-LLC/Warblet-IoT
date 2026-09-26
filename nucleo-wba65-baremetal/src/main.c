/* Blink LD1, service USB setup, and print simulated HTTP reports for a relay.
 * Saved settings survive reset. The app can enter the ROM bootloader. */

#include "board.h"
#include "uart.h"
#include "chirp_prov.h"
#include "chirp_send.h"
#include "dfu.h"

static volatile unsigned s_ms;

void SysTick_Handler(void)
{
  s_ms++;
}

static unsigned millis(void)
{
  return s_ms;
}

static void led_init(void)
{
  RCC->AHB2ENR |= LED_RCC_EN;
  (void)RCC->AHB2ENR;
  LED_PORT->MODER = (LED_PORT->MODER & ~(3u << (LED_PIN * 2u))) | (1u << (LED_PIN * 2u));
  LED_PORT->OTYPER &= ~(1u << LED_PIN);
}

static void led_set(int on)
{
#if LED_ACTIVE_LOW
  LED_PORT->BSRR = on ? (1u << (LED_PIN + 16u)) : (1u << LED_PIN);
#else
  LED_PORT->BSRR = on ? (1u << LED_PIN) : (1u << (LED_PIN + 16u));
#endif
}

static void btn_init(void)
{
  RCC->AHB2ENR |= BTN_RCC_EN;
  (void)RCC->AHB2ENR;
  BTN_PORT->MODER &= ~(3u << (BTN_PIN * 2u));                       /* input */
  BTN_PORT->PUPDR = (BTN_PORT->PUPDR & ~(3u << (BTN_PIN * 2u))) | (1u << (BTN_PIN * 2u));
}

static int btn_down(void)
{
  unsigned in = (BTN_PORT->IDR >> BTN_PIN) & 1u;
#if BTN_ACTIVE_LOW
  return in == 0u;
#else
  return in != 0u;
#endif
}

int main(void)
{
  SysTick->LOAD = (SYSCLK_HZ / 1000u) - 1u;      /* 1 ms */
  SysTick->VAL  = 0u;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

  led_init();
  btn_init();
  uart_init();
  chirp_prov_init();
  chirp_send_init();

  unsigned last_blink = millis();
  unsigned btn_since  = 0u;
  int      btn_prev   = 0;
  int      led_on     = 0;

  for (;;) {
    chirp_prov_poll();

    unsigned now = millis();

    chirp_send_tick(now);

    if ((now - last_blink) >= 500u) {            /* ~1 Hz, 50% duty */
      last_blink = now;
      led_on = !led_on;
      led_set(led_on);
    }

    int down = btn_down();
    if (down && !btn_prev) { btn_since = now; }
    if (down && (now - btn_since) >= 3000u) {
      uart_line("");
      uart_line("CHIRP. B1 held 3s -> system bootloader at 0x0BF90000");
      dfu_enter_system_bootloader();
    }
    btn_prev = down;
  }
}
