/* MQTTS reports and command callbacks.
 * The application supplies payloads; this module manages the session and signatures. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chirp_store.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Base uplink and downlink topics. Append one topic level for an uplink tag. */
#define CHIRP_TOPIC_UP   "chirp/%s/up"
#define CHIRP_TOPIC_DOWN "chirp/%s/down"

/* The dashboard button text, verbatim (§Spec UI config). */
#define CHIRP_CMD_SNAP "snap"

/* Requested frames use the command name as a tag; scheduled frames use heartbeat. */
#define CHIRP_TAG_SNAP      CHIRP_CMD_SNAP
#define CHIRP_TAG_HEARTBEAT "heartbeat"

/* A failed capture is not a failed session; neither stops the other. */
typedef enum {
    CHIRP_SENT,              /* the bytes went to the broker */
    CHIRP_SKIPPED,           /* nothing was sent, and the session is not why */
    CHIRP_CONNECTION_FAILED, /* the publish failed; the session is dropped */
} chirp_send_result_t;

/* Publishes one message, signed if a key is stored. The tag is the last topic
 * level and is covered by the signature. Call it from a chirp_report_fn. */
chirp_send_result_t chirp_mqtt_send_bytes(const uint8_t *bytes, size_t length,
                                          const char *tag);

/* Called every 300 s and once per command. Call chirp_mqtt_send_bytes() with
 * the same `tag` and return its result. */
typedef chirp_send_result_t (*chirp_report_fn)(const char *tag);

/* Return a constant tag pointer to request a report, or NULL to skip it.
 * This callback runs on the MQTT task; capture and publish belong on the send task. */
typedef const char *(*chirp_command_fn)(const char *command);

/* Starts the session and the heartbeat task. *config is copied; `report` is
 * required, and `on_command` may be NULL on a board that takes no commands. */
esp_err_t chirp_mqtt_start(const chirp_config_t *config,
                           chirp_report_fn report,
                           chirp_command_fn on_command);

#ifdef __cplusplus
}
#endif
