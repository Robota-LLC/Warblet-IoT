/* Uplink over the persistent CHIRP1 socket, and the downlink lines pushed back
 * down it. Transport only: what to send lives in main.c. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chirp_store.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Largest payload this demo sends; it sizes the line buffer in chirp_send.c. */
#define CHIRP_PAYLOAD_MAX 64

/* Only a completed write resets the reconnect backoff; it does not prove server acceptance. */
typedef enum {
    CHIRP_SENT,              /* the bytes went out on a working socket */
    CHIRP_SKIPPED,           /* nothing was sent, and the socket is still good */
    CHIRP_CONNECTION_FAILED, /* the socket broke; the loop reconnects */
} chirp_send_result_t;

/* Signs (if a key is stored), base64s and writes one payload. Call it only from
 * a chirp_report_fn: the socket exists only inside that callback. */
chirp_send_result_t chirp_send_bytes(const uint8_t *payload, size_t length);

/* Called every 30 s. Read the sensor, call chirp_send_bytes() and return its
 * result, or CHIRP_SKIPPED when there is nothing to send. */
typedef chirp_send_result_t (*chirp_report_fn)(void);

/* Called with one downlink frame, as text. */
typedef void (*chirp_command_fn)(const char *command);

/* Starts the transport task. *config is copied; `report` is required, and
 * `on_command` may be NULL on a board that takes no commands. */
esp_err_t chirp_send_start(const chirp_config_t *config,
                           chirp_report_fn report,
                           chirp_command_fn on_command);

#ifdef __cplusplus
}
#endif
