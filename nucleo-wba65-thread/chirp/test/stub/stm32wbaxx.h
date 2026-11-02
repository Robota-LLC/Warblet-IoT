#pragma once
#include "stm32wbaxx_hal.h"
extern unsigned test_uid;                   /* the die's unique id, from boottest.c */
#define UID_BASE ((uintptr_t)&test_uid)
/* Interrupt masking, which a host run has nothing to mask. */
static inline uint32_t __get_PRIMASK(void) { return 0u; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t primask) { (void)primask; }
static uint32_t test_basepri __attribute__((unused)), test_basepri_raised __attribute__((unused));
static inline uint32_t __get_BASEPRI(void) { return test_basepri; }
static inline void __set_BASEPRI(uint32_t v) { test_basepri = v; }
static inline void __set_BASEPRI_MAX(uint32_t v) { if (v != 0u && (test_basepri == 0u || v < test_basepri)) { test_basepri = v; test_basepri_raised++; } }
#define __NVIC_PRIO_BITS 4u
#define USART1_IRQn      1
static inline uint32_t NVIC_GetPriority(int irq) { (void)irq; return 7u; }
