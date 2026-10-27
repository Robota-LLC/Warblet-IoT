/* USB console provisioning responder. */
#pragma once

#include "chirp_store.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the console reader with a copy of config for identity and config
 * merging. `slug` names the demo in the announce, as t=<slug>. */
esp_err_t chirp_prov_start(const chirp_config_t *config, const char *slug);

#ifdef __cplusplus
}
#endif
