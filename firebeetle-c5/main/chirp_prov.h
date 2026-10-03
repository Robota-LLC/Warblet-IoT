/* USB console provisioning responder. */
#pragma once

#include "chirp_store.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the console reader with a copy of config for identity and config merging. */
esp_err_t chirp_prov_start(const chirp_config_t *config);

#ifdef __cplusplus
}
#endif
