#include <assert.h>
#include <stdio.h>
#include "camera_health.h"

int main(void)
{
    camera_health_t h;
    camera_health_reset(&h);
    assert(camera_health_begin_init(&h, 0));
    assert(!camera_health_begin_init(&h, 1));
    assert(!camera_health_begin_frame(&h, 1));
    assert(camera_health_complete(&h, -1));
    assert(h.state == CAMERA_UNAVAILABLE && h.attempts == 1);
    assert(!camera_health_begin_init(&h, 60099));
    assert(!camera_health_begin_init(&h, 999999999));

    /* Restart resets the budget; a functioning sensor can capture again. */
    camera_health_reset(&h);
    assert(camera_health_begin_init(&h, 0));
    assert(camera_health_complete(&h, 0));
    assert(h.state == CAMERA_READY && h.error == 0);
    assert(camera_health_begin_frame(&h, 100));
    assert(!camera_health_begin_frame(&h, 101));
    assert(camera_health_complete(&h, -1));
    assert(!camera_health_begin_init(&h, 999999999));
    assert(h.attempts == 1 && h.state == CAMERA_UNAVAILABLE);

    camera_health_reset(&h);
    assert(camera_health_begin_init(&h, 100));
    assert(!camera_health_timeout(&h, 263, 20099));
    assert(camera_health_timeout(&h, 263, 20100));
    assert(h.state == CAMERA_STALLED && h.error == 263);
    assert(!camera_health_complete(&h, 0));
    assert(!camera_health_begin_init(&h, 999999999));
    assert(!camera_health_begin_frame(&h, 999999999));
    assert(!camera_health_timeout(&h, 263, 999999999));

    camera_health_reset(&h);
    assert(camera_health_begin_init(&h, 0));
    assert(camera_health_complete(&h, 0));
    assert(camera_health_begin_frame(&h, 200));
    assert(camera_health_timeout(&h, 263, 20200));
    assert(!camera_health_complete(&h, 0));
    assert(h.state == CAMERA_STALLED);
    puts("PASS: failure/recovery, retry budget, no overlap, init/capture timeout quarantine");
}
