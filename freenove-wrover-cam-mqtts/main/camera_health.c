#include "camera_health.h"

void camera_health_reset(camera_health_t *h)
{
    *h = (camera_health_t){ .state = CAMERA_INITIALIZING };
}

bool camera_health_begin_init(camera_health_t *h, int64_t now)
{
    if (h->operation != CAMERA_IDLE || h->state == CAMERA_READY ||
        h->state == CAMERA_STALLED || h->attempts >= CAMERA_MAX_ATTEMPTS) {
        return false;
    }
    h->attempts++;
    h->state = CAMERA_INITIALIZING;
    h->operation = CAMERA_INIT;
    h->started_ms = now;
    return true;
}

bool camera_health_begin_frame(camera_health_t *h, int64_t now)
{
    if (h->state != CAMERA_READY || h->operation != CAMERA_IDLE) return false;
    h->operation = CAMERA_FRAME;
    h->started_ms = now;
    return true;
}

bool camera_health_complete(camera_health_t *h, int error)
{
    /* Never reuse a timed-out driver, even if a late completion arrives. */
    if (h->state == CAMERA_STALLED || h->operation == CAMERA_IDLE) return false;
    h->operation = CAMERA_IDLE;
    h->error = error;
    h->state = error == 0 ? CAMERA_READY : CAMERA_UNAVAILABLE;
    return true;
}

bool camera_health_timeout(camera_health_t *h, int timeout_error, int64_t now)
{
    if (h->state == CAMERA_STALLED || h->operation == CAMERA_IDLE ||
        now - h->started_ms < CAMERA_OPERATION_MS) return false;
    h->state = CAMERA_STALLED;
    h->error = timeout_error;
    /* Keep operation occupied: deleting a driver task could leave a lock held. */
    return true;
}

const char *camera_health_state(const camera_health_t *h)
{
    switch (h->state) {
    case CAMERA_INITIALIZING: return "initializing";
    case CAMERA_READY: return "ready";
    case CAMERA_UNAVAILABLE: return "unavailable";
    default: return "stalled";
    }
}
