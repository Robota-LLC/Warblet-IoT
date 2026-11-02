#pragma once
#include "stm32wbaxx_hal.h"
extern unsigned test_uid;                   /* the die's unique id, from boottest.c */
#define UID_BASE ((uintptr_t)&test_uid)
