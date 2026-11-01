/* Capture scheduled and requested JPEG frames, then publish over MQTTS.
 * The camera module handles sensor setup and buffer ownership. */
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "camera_health.h"
#include "chirp_mqtt.h"
#include "warblet.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "warblet";

/* What this demo is: its catalogue id (sent in the USB announce), its
 * default id, where it reports, and its route. */
static const warblet_demo_t DEMO = {
    .slug         = "demo-wrover-cam-mqtts",
    .hwid_prefix  = "cam-",                 /* the default id: this + the MAC's last three bytes */
    .default_host = "mqtt.warbletiot.com",  /* a host the setup page stored wins */
    .transports   = "\"mqtts\"",
};

/* Health goes out when it changes, and every 5 minutes while the camera is
 * not ready. A frame that was sent also counts as health. */
#define HEALTH_PERIOD_MS 300000

/* ---------------------------------------------------------------------------
 * the camera
 * ------------------------------------------------------------------------ */

/* snap: the frame answers a Snap command, so a failure is reported as one. */
typedef struct { camera_operation_t operation; const char *tag; bool snap; } camera_job_t;
typedef struct {
    camera_job_t job;
    esp_err_t error;
    const uint8_t *frame;
    size_t length;
    int64_t completed_ms;
} camera_done_t;

static QueueHandle_t s_jobs, s_done;
static camera_health_t s_health;
static bool s_health_dirty = true;
static bool s_worker_available;
static bool s_pending_snap;
static int64_t s_next_health;

/* Only this worker starts a driver operation. Run above idle so synchronous
 * I2C completions wake it promptly; networking still has higher priority.
 * A timed-out operation is quarantined, never deleted or overlapped. */
static void camera_worker(void *arg)
{
    camera_job_t job;
    for (;;) {
        xQueueReceive(s_jobs, &job, portMAX_DELAY);
        camera_done_t done = { .job = job };
        if (job.operation == CAMERA_INIT) {
            done.error = camera_init();
        } else {
            /* A failed capture skips this frame; the camera stays on. */
            done.error = camera_capture(&done.frame, &done.length);
        }
        done.completed_ms = esp_timer_get_time() / 1000;
        xQueueSend(s_done, &done, portMAX_DELAY);
    }
}

static void request_frame(const char *tag, int64_t now)
{
    bool snap = strcmp(tag, CHIRP_TAG_SNAP) == 0;
    if (camera_health_begin_frame(&s_health, now)) {
        s_pending_snap = snap;
        camera_job_t job = { CAMERA_FRAME, tag, snap };
        xQueueSend(s_jobs, &job, 0); /* one outstanding operation, queue is empty */
    } else if (snap) {
        s_health.snap_failed = true;
        s_health_dirty = true;
        ESP_LOGW(TAG, "snap failed: camera %s or busy", camera_health_state(&s_health));
    }
}

/* The MQTT task calls health once a second and image requests independently.
 * All waits inside the camera driver belong to camera_worker, never here. */
static chirp_send_result_t report_camera(const char *tag)
{
    if (tag == NULL) {
        s_health_dirty = true; /* first health after each MQTT connection */
        tag = CHIRP_TAG_HEALTH;
    }
    int64_t now = esp_timer_get_time() / 1000;
    chirp_send_result_t result = CHIRP_SKIPPED;
    camera_done_t done;
    if (s_worker_available && xQueueReceive(s_done, &done, 0) == pdTRUE) {
        camera_state_t previous_state = s_health.state;
        int previous_error = s_health.error;
        camera_health_timeout(&s_health, ESP_ERR_TIMEOUT, done.completed_ms);
        bool completed = camera_health_complete(&s_health, done.error);
        if (s_health.state != previous_state || s_health.error != previous_error ||
            done.job.snap)
            s_health_dirty = true;
        if (done.job.operation == CAMERA_FRAME) {
            if (completed && done.error == ESP_OK) {
                result = chirp_mqtt_send_bytes(done.frame, done.length, done.job.tag);
                /* The JPEG decoder carries readiness and clears prior failure.
                 * Keep the successful image latest for raw_latest() consumers. */
                if (result == CHIRP_SENT) {
                    s_health.snap_failed = false;
                    s_health_dirty = false;
                    s_next_health = now + HEALTH_PERIOD_MS;
                } else if (done.job.snap) {
                    s_health.snap_failed = true;
                    s_health_dirty = true;
                }
            } else if (done.job.snap) {
                s_health.snap_failed = true;
            }
            camera_release();
        } else if (completed && done.error == ESP_OK) {
            request_frame(CHIRP_TAG_HEARTBEAT, now);
        }
    }
    /* Publishing a completed frame can consume the MQTT socket timeout.
     * A newly queued operation starts now, not before that blocking send. */
    now = esp_timer_get_time() / 1000;
    if (camera_health_timeout(&s_health, ESP_ERR_TIMEOUT, now)) {
        s_health_dirty = true;
        if (s_pending_snap) s_health.snap_failed = true;
        ESP_LOGE(TAG, "camera operation timed out; driver quarantined until restart");
    }
    if (camera_health_begin_init(&s_health, now)) {
        camera_job_t job = { CAMERA_INIT, NULL, false };
        xQueueSend(s_jobs, &job, 0);
        s_health_dirty = true;
    }
    if (strcmp(tag, CHIRP_TAG_HEALTH) != 0) {
        request_frame(tag, now);
    }
    if (result != CHIRP_CONNECTION_FAILED &&
        (s_health_dirty || (s_health.state != CAMERA_READY && now >= s_next_health))) {
        char payload[192];
        int length = snprintf(payload, sizeof(payload),
            "{\"warbletCameraHealth\":1,\"camera\":\"%s\",\"error\":%d,"
            "\"attempts\":%u,\"uptime_s\":%" PRId64 ",\"snap\":\"%s\"}",
            camera_health_state(&s_health), s_health.error, s_health.attempts,
            now / 1000, s_health.snap_failed ? "failed" : "none");
        chirp_send_result_t health_result = chirp_mqtt_send_bytes(
            (const uint8_t *)payload, (size_t)length, CHIRP_TAG_HEALTH);
        if (health_result == CHIRP_SENT) {
            s_health_dirty = false;
            s_next_health = now + HEALTH_PERIOD_MS;
        }
        if (health_result == CHIRP_CONNECTION_FAILED) result = health_result;
    }
    return result;
}

/* Return the response tag for a recognized command, or NULL.
 * The send task performs the capture. */
static const char *handle_command(const char *command)
{
    if (strcmp(command, CHIRP_CMD_SNAP) == 0) {
        ESP_LOGI(TAG, "down %s -- capturing now", CHIRP_CMD_SNAP);
        return CHIRP_TAG_SNAP;
    }
    ESP_LOGI(TAG, "downlink ignored: %s", command);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * start-up
 * ------------------------------------------------------------------------ */

/* Use blocking UART0 input through the CH340 bridge for serial setup.
 * Keep LF line endings; the parser strips a trailing CR. */
static void console_init(void)
{
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 1024, 0, 0, NULL, 0));

    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* stdin does not block, so the setup reader can tell a line that
     * stopped coming; stdout does, so an answer is never cut short. */
    fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);
    fcntl(fileno(stdout), F_SETFL, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
}

void app_main(void)
{
    ESP_ERROR_CHECK(warblet_start(&DEMO));
    console_init();

    chirp_config_t config;
    ESP_ERROR_CHECK(chirp_store_load(&config));
    ESP_LOGI(TAG, "hwid=%s host=%s credential=%s signing-key=%s",
             chirp_store_hwid(&config), chirp_store_host(&config),
             config.cred_kind == CHIRP_CRED_CLAIM ? "claim" :
             config.cred_kind == CHIRP_CRED_TOKEN ? "token" : "none",
             config.has_key ? "yes" : "no");

    /* Always listening, set up or not. */
    ESP_ERROR_CHECK(chirp_prov_start(&config));

    /* A network and a credential, or nothing is sent: this demo has no Open mode. */
    if (config.ssid[0] == '\0' || config.cred_kind == CHIRP_CRED_NONE) {
        ESP_LOGI(TAG, "not set up yet, waiting for the setup page");
        return;
    }
    ESP_ERROR_CHECK(wifi_ladder_start(&config));

    camera_health_reset(&s_health);
    s_jobs = xQueueCreate(1, sizeof(camera_job_t));
    s_done = xQueueCreate(1, sizeof(camera_done_t));
    if (s_jobs == NULL || s_done == NULL ||
        xTaskCreatePinnedToCore(camera_worker, "camera_worker", 6144, NULL,
                               tskIDLE_PRIORITY + 1, NULL, 1) != pdPASS) {
        s_health.state = CAMERA_STALLED;
        s_health.error = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "camera worker unavailable; networking remains active");
    } else {
        s_worker_available = true;
    }

    /* A stored host esp-mqtt cannot use stops MQTT, not the board: USB setup
     * keeps listening, so a new push can put it right. */
    esp_err_t err = chirp_mqtt_start(&config, report_camera, handle_command);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT not started (%s); push a working host over USB setup",
                 esp_err_to_name(err));
    }
}
