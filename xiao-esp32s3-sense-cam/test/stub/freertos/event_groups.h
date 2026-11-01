/* Host stand-in (test only): enough for warblet.c to compile; never called. */
#pragma once
#include "FreeRTOS.h"
typedef void    *EventGroupHandle_t;
typedef uint32_t EventBits_t;
#define BIT0 0x1u
#define BIT1 0x2u
static inline EventGroupHandle_t xEventGroupCreate(void) { return (EventGroupHandle_t)1; }
static inline EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t b) { (void)g; return b; }
static inline EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t b) { (void)g; return b; }
static inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t b, int c, int a, TickType_t t)
{ (void)g; (void)b; (void)c; (void)a; (void)t; return 0; }
