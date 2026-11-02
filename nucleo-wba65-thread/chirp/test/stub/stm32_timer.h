#pragma once
#include <stdint.h>
typedef enum { UTIL_TIMER_OK = 0, UTIL_TIMER_INVALID_PARAM = 1 } UTIL_TIMER_Status_t;
typedef enum { UTIL_TIMER_ONESHOT = 0, UTIL_TIMER_PERIODIC = 1 } UTIL_TIMER_Mode_t;
typedef struct {
  uint32_t period, due;
  int      mode, running;
  void   (*callback)(void *arg);
  void    *arg;
} UTIL_TIMER_Object_t;
UTIL_TIMER_Status_t UTIL_TIMER_Create(UTIL_TIMER_Object_t *t, uint32_t period, UTIL_TIMER_Mode_t mode,
                                      void (*callback)(void *arg), void *arg);
UTIL_TIMER_Status_t UTIL_TIMER_Start(UTIL_TIMER_Object_t *t);
UTIL_TIMER_Status_t UTIL_TIMER_Stop(UTIL_TIMER_Object_t *t);
