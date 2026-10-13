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
    assert(camera_health_complete(&h, -1, 100));
    assert(h.state == CAMERA_UNAVAILABLE && h.attempts == 1);
    assert(!camera_health_begin_init(&h, 60099));
    assert(camera_health_begin_init(&h, 60100));
    assert(camera_health_complete(&h, 0, 60200));
    assert(h.state == CAMERA_READY && h.error == 0);
    assert(camera_health_begin_frame(&h, 60200));
    assert(!camera_health_begin_frame(&h, 60201));
    assert(camera_health_complete(&h, -1, 60300));
    assert(!camera_health_begin_init(&h, 120299));
    assert(camera_health_begin_init(&h, 120300));
    assert(camera_health_complete(&h, -1, 120400));
    assert(!camera_health_begin_init(&h, 999999999));
    assert(h.attempts == 3 && h.state == CAMERA_UNAVAILABLE);

    camera_health_reset(&h);
    assert(camera_health_begin_init(&h, 100));
    assert(!camera_health_timeout(&h, 263, 20099));
    assert(camera_health_timeout(&h, 263, 20100));
    assert(h.state == CAMERA_STALLED && h.error == 263);
    assert(!camera_health_complete(&h, 0, 20200));
    assert(!camera_health_begin_init(&h, 999999999));
    assert(!camera_health_begin_frame(&h, 999999999));
    assert(!camera_health_timeout(&h, 263, 999999999));

    camera_health_reset(&h);
    assert(camera_health_begin_init(&h, 0));
    assert(camera_health_complete(&h, 0, 100));
    assert(camera_health_begin_frame(&h, 200));
    assert(camera_health_timeout(&h, 263, 20200));
    assert(!camera_health_complete(&h, 0, 20300));
    assert(h.state == CAMERA_STALLED);
    puts("PASS: failure/recovery, retry budget, no overlap, init/capture timeout quarantine");
}
