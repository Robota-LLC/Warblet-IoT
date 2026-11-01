/* Host stand-ins for the ESP-IDF headers warblet.c includes (test only). */
#pragma once
#include <stdio.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK                        0
#define ESP_FAIL                      (-1)
#define ESP_ERR_NO_MEM                0x101
#define ESP_ERR_INVALID_ARG           0x102
#define ESP_ERR_INVALID_SIZE          0x104
#define ESP_ERR_NVS_NOT_FOUND         0x1102
#define ESP_ERR_NVS_NO_FREE_PAGES     0x110d
#define ESP_ERR_NVS_INVALID_LENGTH    0x110c
#define ESP_ERR_NVS_REMOVE_FAILED     0x1112
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "error"; }
#define ESP_ERROR_CHECK(x) do { esp_err_t e_ = (x); if (e_ != ESP_OK) { fprintf(stderr, "ESP_ERROR_CHECK %d\n", e_); abort(); } } while (0)
