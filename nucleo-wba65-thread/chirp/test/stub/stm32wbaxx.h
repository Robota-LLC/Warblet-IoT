#pragma once
#include "stm32wbaxx_hal.h"
extern unsigned test_uid;                   /* the die's unique id, from boottest.c */
#define UID_BASE ((uintptr_t)&test_uid)
/* Interrupt masking, which a host run has nothing to mask. */
static inline uint32_t __get_PRIMASK(void) { return 0u; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t primask) { (void)primask; }
