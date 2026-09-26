#pragma once
#include <stdint.h>
#define UTIL_SEQ_RFU 0u
void UTIL_SEQ_SetTask(uint32_t task, uint32_t prio);
void UTIL_SEQ_RegTask(uint32_t task, uint32_t flags, void (*fn)(void));
