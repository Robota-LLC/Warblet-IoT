/* system.c — the whole "clock setup": keep the reset tree (HSI16, 16 MHz) and
 * point VTOR at our own vector table. The watchdog is never touched. */

#include "stm32wba65xx.h"
#include "board.h"

uint32_t SystemCoreClock = SYSCLK_HZ;

extern const void *g_vectors;

void SystemInit(void)
{
  /* Vector table at the start of our image (0x08000000 for a plain DFU flash). */
  SCB->VTOR = (uint32_t)&g_vectors;

  /* FPU left off: the build is soft-float. */
}

/* Required by system_stm32wbaxx.h's prototype set; we never change clocks. */
void SystemCoreClockUpdate(void)
{
  SystemCoreClock = SYSCLK_HZ;
}
