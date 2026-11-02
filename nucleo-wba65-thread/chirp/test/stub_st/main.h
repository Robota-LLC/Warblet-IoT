/* Host stand-in for the "main.h" ST's openthread/platform/uart.c includes
 * (echotest only): what that file uses from it, nothing more. */
#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "stm32wbaxx_hal.h"

#define OT_CLI_USE 1
#define UNUSED(x)  ((void)(x))
#define TRUE       true
#define FALSE      false
#define __WEAK     __attribute__((weak))
