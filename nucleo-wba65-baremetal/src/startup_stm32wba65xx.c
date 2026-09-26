/* startup_stm32wba65xx.c — vector table + reset entry, no CubeMX, no HAL.
 * IRQ count: WBA65 IRQn_Type runs 0..81 (EXTI20_RADIO_IO_IRQn = 81), so the
 * table is 16 system entries + 82 device entries. */

#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int  main(void);
void SystemInit(void);

void Reset_Handler(void);
static void Default_Handler(void);

void NMI_Handler(void)            __attribute__((weak, alias("_default_isr")));
void HardFault_Handler(void)      __attribute__((weak, alias("_default_isr")));
void MemManage_Handler(void)      __attribute__((weak, alias("_default_isr")));
void BusFault_Handler(void)       __attribute__((weak, alias("_default_isr")));
void UsageFault_Handler(void)     __attribute__((weak, alias("_default_isr")));
void SecureFault_Handler(void)    __attribute__((weak, alias("_default_isr")));
void SVC_Handler(void)            __attribute__((weak, alias("_default_isr")));
void DebugMon_Handler(void)       __attribute__((weak, alias("_default_isr")));
void PendSV_Handler(void)         __attribute__((weak, alias("_default_isr")));
void SysTick_Handler(void);

void _default_isr(void);
void _default_isr(void) { Default_Handler(); }

static void Default_Handler(void)
{
  /* Stop on an unhandled interrupt so a debugger can inspect it. */
  for (;;) { __asm volatile("wfi"); }
}

typedef void (*vector_t)(void);

#define DEV_IRQS 82

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + DEV_IRQS] = {
  (vector_t)(&_estack),
  Reset_Handler,
  NMI_Handler,
  HardFault_Handler,
  MemManage_Handler,
  BusFault_Handler,
  UsageFault_Handler,
  SecureFault_Handler,
  0, 0, 0,
  SVC_Handler,
  DebugMon_Handler,
  0,
  PendSV_Handler,
  SysTick_Handler,
  /* No device IRQs are used (RX is polled); all park here. */
  [16 ... (16 + DEV_IRQS - 1)] = _default_isr,
};

void Reset_Handler(void)
{
  uint32_t *src = &_sidata, *dst = &_sdata;
  while (dst < &_edata) { *dst++ = *src++; }
  for (dst = &_sbss; dst < &_ebss; ) { *dst++ = 0u; }

  SystemInit();
  (void)main();
  for (;;) { __asm volatile("wfi"); }
}
