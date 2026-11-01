/* Host stand-in (test only): the version the build would set from PROJECT_VER. */
#pragma once
typedef struct {
    char version[32];
} esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
