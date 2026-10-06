/* Stop Thread, IPv6, interrupts, DMA, and peripheral clocks before entering ROM DFU.
 * The bootloader entry is SYSTEM_FLASH_BASE_NS (0x0BF90000).
 * If USB does not enumerate, power-cycle with BOOT0 configured for the bootloader. */

#include "chirp_dfu.h"

#include "stm32wbaxx.h"
#include "stm32wbaxx_hal.h"

#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/ip6.h"

#include "ll_sys.h"

typedef void (*entry_fn)(void);

void chirp_dfu_enter(otInstance *inst)
{
  volatile const unsigned *sysmem = (volatile const unsigned *)SYSTEM_FLASH_BASE_NS;
  unsigned msp;
  entry_fn entry;

  /* ---- 1..3: OpenThread down, radio off, instance released ---------------- */
  if (inst != NULL) {
    if (otThreadGetDeviceRole(inst) != OT_DEVICE_ROLE_DISABLED) {
      (void)otThreadSetEnabled(inst, false);
    }
    (void)otIp6SetEnabled(inst, false);
    otInstanceFinalize(inst);
  }

  /* ---- 4: ST link layer stops interrupting -------------------------------- */
  ll_sys_disable_irq();

  /* ---- 5: everything masked ------------------------------------------------ */
  __disable_irq();

  /* ---- 6: SysTick off ------------------------------------------------------ */
  SysTick->CTRL = 0u;
  SysTick->LOAD = 0u;
  SysTick->VAL  = 0u;

  for (unsigned i = 0; i < 8u; i++) {
    NVIC->ICER[i] = 0xFFFFFFFFu;
    NVIC->ICPR[i] = 0xFFFFFFFFu;
  }

  /* ---- 7,8: peripherals and clock tree back to reset ----------------------- */
  (void)HAL_DeInit();
  (void)HAL_RCC_DeInit();

  __DSB();
  __ISB();

  /* Read the ROM's vector table after the clock tree is back at reset. */
  msp   = sysmem[0];
  entry = (entry_fn)sysmem[1];

  SCB->VTOR = (unsigned)SYSTEM_FLASH_BASE_NS;
  __DSB();
  __ISB();

  __set_CONTROL(0u);        /* privileged, MSP */
  __ISB();
  __set_MSP(msp);
  __ISB();

  entry();

  for (;;) { }              /* the ROM never returns */
}
