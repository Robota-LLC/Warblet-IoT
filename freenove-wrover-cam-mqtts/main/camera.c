#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include "camera.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "camera";

/* Freenove ESP32-WROVER CAM pin map.
 * PWDN and RESET are not connected. The camera driver owns the SCCB pins. */
#define CAM_PIN_PWDN  (-1)
#define CAM_PIN_RESET (-1)
#define CAM_PIN_XCLK  21
#define CAM_PIN_SIOD  26
#define CAM_PIN_SIOC  27
#define CAM_PIN_D7    35
#define CAM_PIN_D6    34
#define CAM_PIN_D5    39
#define CAM_PIN_D4    36
#define CAM_PIN_D3    19
#define CAM_PIN_D2    18
#define CAM_PIN_D1     5
#define CAM_PIN_D0     4
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF  23
#define CAM_PIN_PCLK  22

/* Flip RGB565 rows horizontally before software JPEG encoding.
 * Set to zero to preserve the sensor's orientation; hardware JPEG is unaffected. */
#define CAM_HMIRROR 1

/* Two scales pointing opposite ways: the sensor takes 0 (best) to 63 (worst),
 * the software encoder 1 (worst) to 100 (best). */
#define SENSOR_JPEG_QUALITY   12
#define SOFTWARE_JPEG_QUALITY 80

/* Frame age threshold for requesting another capture. */
#define FRAME_STALE_MS 2000
#define FRAME_TRIES    3

static camera_fb_t *s_fb;
static uint8_t     *s_jpeg;       /* non-NULL only when we encoded it ourselves */
static bool         s_soft_jpeg;  /* this sensor has no JPEG encoder */

esp_err_t camera_init(void)
{
    camera_config_t cfg = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7       = CAM_PIN_D7,
        .pin_d6       = CAM_PIN_D6,
        .pin_d5       = CAM_PIN_D5,
        .pin_d4       = CAM_PIN_D4,
        .pin_d3       = CAM_PIN_D3,
        .pin_d2       = CAM_PIN_D2,
        .pin_d1       = CAM_PIN_D1,
        .pin_d0       = CAM_PIN_D0,
        .pin_vsync    = CAM_PIN_VSYNC,
        .pin_href     = CAM_PIN_HREF,
        .pin_pclk     = CAM_PIN_PCLK,

        .xclk_freq_hz = 10000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        /* JPEG straight from the sensor, when the fitted sensor can. */
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_QVGA,
        .jpeg_quality = SENSOR_JPEG_QUALITY,

        /* Keep one frame buffer in PSRAM. Capture checks its age before returning it. */
        .fb_count     = 1,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "this sensor has no JPEG encoder — capturing RGB565 and "
                      "encoding in software instead");
        cfg.pixel_format = PIXFORMAT_RGB565;
        err = esp_camera_init(&cfg);
        s_soft_jpeg = (err == ESP_OK);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init: %s — check the ribbon seating and that "
                      "PSRAM is enabled", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    ESP_LOGI(TAG, "sensor PID 0x%02x up, QVGA JPEG, encoded %s",
             s != NULL ? s->id.PID : 0,
             s_soft_jpeg ? "in software" : "by the sensor");
    return ESP_OK;
}

/* Frame age in ms against the driver's clock. */
static int64_t fb_age_ms(const camera_fb_t *fb)
{
    int64_t taken = (int64_t)fb->timestamp.tv_sec * 1000 +
                    (int64_t)fb->timestamp.tv_usec / 1000;
    return esp_timer_get_time() / 1000 - taken;
}

/* Discard stale queued frames, up to FRAME_TRIES captures.
 * Return the last frame with a warning if it still exceeds FRAME_STALE_MS. */
esp_err_t camera_capture(const uint8_t **out, size_t *out_len)
{
    if (out == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    camera_release();  /* a caller that forgot is not a reason to leak */

    int64_t age = 0;
    for (int attempt = 1; ; attempt++) {
        s_fb = esp_camera_fb_get();
        if (s_fb == NULL) {
            return ESP_FAIL;
        }
        age = fb_age_ms(s_fb);
        if (age <= FRAME_STALE_MS || attempt >= FRAME_TRIES) {
            break;
        }
        ESP_LOGW(TAG, "frame age %" PRId64 " ms -- parked before we asked, "
                      "dropping it (try %d)", age, attempt);
        esp_camera_fb_return(s_fb);
        s_fb = NULL;
    }
    if (age > FRAME_STALE_MS) {
        ESP_LOGW(TAG, "frame age %" PRId64 " ms after %d tries -- posting it "
                      "anyway; the sensor is delivering slowly", age,
                 FRAME_TRIES);
    } else {
        ESP_LOGI(TAG, "frame age %" PRId64 " ms", age);
    }

    const uint8_t *buf = s_fb->buf;
    size_t         len = s_fb->len;

    if (s_soft_jpeg) {
#if CAM_HMIRROR
        /* Reverse RGB565 rows in place before software JPEG encoding. */
        uint16_t *px = (uint16_t *)s_fb->buf;
        for (size_t y = 0; y < s_fb->height; y++) {
            uint16_t *row = px + (y * s_fb->width);
            for (size_t i = 0, j = s_fb->width - 1; i < j; i++, j--) {
                uint16_t t = row[i];
                row[i] = row[j];
                row[j] = t;
            }
        }
#endif
        /* frame2jpg allocates the output; camera_release() frees it. */
        if (!frame2jpg(s_fb, SOFTWARE_JPEG_QUALITY, &s_jpeg, &len)) {
            ESP_LOGE(TAG, "software JPEG encode failed");
            camera_release();
            return ESP_FAIL;
        }
        buf = s_jpeg;
    }

    /* The portal sniffs FF D8 FF to treat a message as an image; never post
     * something that is not one. */
    if (len < 3 || buf[0] != 0xff || buf[1] != 0xd8 || buf[2] != 0xff) {
        ESP_LOGE(TAG, "frame is not a JPEG (%u bytes)", (unsigned)len);
        camera_release();
        return ESP_ERR_INVALID_STATE;
    }

    *out     = buf;
    *out_len = len;
    return ESP_OK;
}

void camera_release(void)
{
    if (s_jpeg != NULL) {
        free(s_jpeg);
        s_jpeg = NULL;
    }
    if (s_fb != NULL) {
        esp_camera_fb_return(s_fb);
        s_fb = NULL;
    }
}
