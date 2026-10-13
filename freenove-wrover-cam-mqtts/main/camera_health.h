#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CAMERA_MAX_ATTEMPTS 1
#define CAMERA_OPERATION_MS 20000

typedef enum { CAMERA_INITIALIZING, CAMERA_READY, CAMERA_UNAVAILABLE, CAMERA_STALLED } camera_state_t;
typedef enum { CAMERA_IDLE, CAMERA_INIT, CAMERA_FRAME } camera_operation_t;
typedef struct {
    camera_state_t state;
    camera_operation_t operation;
    unsigned attempts;
    int error;
    bool snap_failed;
    int64_t started_ms;
} camera_health_t;

/* Single owner: the network report task. The camera worker returns completions. */
void camera_health_reset(camera_health_t *h);
bool camera_health_begin_init(camera_health_t *h, int64_t now);
bool camera_health_begin_frame(camera_health_t *h, int64_t now);
bool camera_health_complete(camera_health_t *h, int error);
bool camera_health_timeout(camera_health_t *h, int timeout_error, int64_t now);
const char *camera_health_state(const camera_health_t *h);
