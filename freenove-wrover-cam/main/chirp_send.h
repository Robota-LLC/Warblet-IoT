/* Periodic HTTP reports and downlink polling.
 * Callbacks provide payload bytes and handle commands. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chirp_store.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dashboard button text must match this command string. */
#define CHIRP_CMD_SNAP "snap"

/* Tag requested frames with the command name and scheduled frames as heartbeat. */
#define CHIRP_TAG_SNAP      CHIRP_CMD_SNAP
#define CHIRP_TAG_HEARTBEAT "heartbeat"

/* A failed capture is not a failed network; neither stops the other. */
typedef enum {
    CHIRP_SENT,              /* the bytes went out and the server took them */
    CHIRP_SKIPPED,           /* nothing was sent, and the network is not why */
    CHIRP_CONNECTION_FAILED, /* the request did not complete */
} chirp_send_result_t;

/* Signs (if a key is stored), tags and POSTs one message. The tag is covered
 * by the signature. Call it from a chirp_report_fn. */
chirp_send_result_t chirp_send_bytes(const uint8_t *bytes, size_t length,
                                     const char *tag);

/* Called every 60 s and once per command. Call chirp_send_bytes() with the
 * same `tag` and return its result. */
typedef chirp_send_result_t (*chirp_report_fn)(const char *tag);

/* Called with one downlink command. Return the tag to report with at once, or
 * NULL. The string must outlive the call. */
typedef const char *(*chirp_command_fn)(const char *command);

/* Starts the 60 s send task. *config is copied; `report` is required, and
 * `on_command` may be NULL on a board that takes no commands. */
esp_err_t chirp_send_start(const chirp_config_t *config,
                           chirp_report_fn report,
                           chirp_command_fn on_command);

#ifdef __cplusplus
}
#endif
