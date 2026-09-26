/* Jump to the STM32 ROM bootloader at SYSTEM_FLASH_BASE_NS (0x0BF90000).
 * BOOT0 is also available at CN1 pin 9 and CN3 pin 7. */

#include "board.h"
#include "uart.h"
#include "dfu.h"

typedef void (*entry_fn)(void);

void dfu_enter_system_bootloader(void)
{
  volatile const unsigned *sysmem = (volatile const unsigned *)SYSMEM_BASE;
  unsigned msp = sysmem[0];
  entry_fn entry = (entry_fn)sysmem[1];

  uart_flush();

  __disable_irq();

  /* SysTick off — a live tick would fire inside the ROM before it is ready. */
  SysTick->CTRL = 0u;
  SysTick->LOAD = 0u;
  SysTick->VAL  = 0u;

  /* Disable and clear every IRQ so a warm entry inherits nothing. */
  for (unsigned i = 0; i < 8u; i++) {
    NVIC->ICER[i] = 0xFFFFFFFFu;
    NVIC->ICPR[i] = 0xFFFFFFFFu;
  }

  /* Release the peripherals this demo touched; USB was never initialised. */
  VCP_UART->CR1 = 0u;
  RCC->APB2ENR &= ~(unsigned)VCP_RCC_EN;
  RCC->AHB2ENR &= ~(unsigned)(VCP_TX_RCC_EN | VCP_RX_RCC_EN | LED_RCC_EN | BTN_RCC_EN);

  /* HSI16 on and selected; everything else is still at reset. */
  RCC->CR |= RCC_CR_HSION;
  while ((RCC->CR & RCC_CR_HSIRDY) == 0u) { }
  RCC->CFGR1 &= ~RCC_CFGR1_SW_Msk;
  while ((RCC->CFGR1 & RCC_CFGR1_SWS_Msk) != 0u) { }

  __DSB();
  __ISB();

  SCB->VTOR = (unsigned)SYSMEM_BASE;
  __DSB();
  __ISB();

  __set_CONTROL(0u);        /* privileged, MSP */
  __ISB();
  __set_MSP(msp);
  __ISB();

  entry();

  for (;;) { }              /* the ROM never returns */
}
