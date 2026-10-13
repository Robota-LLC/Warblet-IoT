/* Exercise the production report/startup code with fake queues and a clock. */
#include "../main/main.c"
#include <assert.h>

struct fake_queue { unsigned size; bool full; unsigned char data[256]; };
static int64_t clock_ms;
static bool fail_worker;
static unsigned wifi_started, mqtt_started, camera_calls, health_sent, images_sent, released;
static char last_health[192];
static char sequence[256];
static unsigned sequence_size;
static chirp_send_result_t publish_result = CHIRP_SENT;

QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
    assert(count == 1 && size <= 256);
    QueueHandle_t q = calloc(1, sizeof(*q)); q->size = size; return q;
}
int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks) {
    assert(q); if (!q->full) return 0; memcpy(item, q->data, q->size); q->full = false; return 1;
}
int xQueueSend(QueueHandle_t q, const void *item, unsigned ticks) {
    assert(q && !q->full); memcpy(q->data, item, q->size); q->full = true; return 1;
}
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle, int core) {
    assert(wifi_started && priority == tskIDLE_PRIORITY && core == 1); return fail_worker ? 0 : pdPASS;
}
int64_t esp_timer_get_time(void) { return clock_ms * 1000; }
int uart_driver_install(int port, int rx, int tx, int count, void *q, int flags) { return ESP_OK; }
void uart_vfs_dev_port_set_rx_line_endings(int port, int endings) {}
void uart_vfs_dev_port_set_tx_line_endings(int port, int endings) {}
void uart_vfs_dev_use_driver(int port) {}
esp_err_t camera_init(void) { camera_calls++; return ESP_FAIL; }
esp_err_t camera_capture(const uint8_t **p, size_t *len) { camera_calls++; return ESP_FAIL; }
void camera_release(void) { released++; }
void camera_deinit(void) {}
esp_err_t chirp_store_init(void) { return ESP_OK; }
esp_err_t chirp_store_load(chirp_config_t *out) { *out = (chirp_config_t){ .ap_count = 1 }; return ESP_OK; }
const char *chirp_store_hwid(const chirp_config_t *cfg) { return "test-camera"; }
esp_err_t chirp_prov_start(const chirp_config_t *cfg) { return ESP_OK; }
esp_err_t wifi_ladder_start(const chirp_config_t *cfg) { wifi_started++; return ESP_OK; }
esp_err_t chirp_mqtt_start(const chirp_config_t *cfg, chirp_report_fn report, chirp_command_fn command) {
    assert(wifi_started && camera_calls == 0); mqtt_started++; return ESP_OK;
}
chirp_send_result_t chirp_mqtt_send_bytes(const uint8_t *bytes, size_t length, const char *tag) {
    if (!strcmp(tag, "health")) { assert(length < sizeof(last_health)); memcpy(last_health, bytes, length); last_health[length] = 0; health_sent++; }
    else images_sent++;
    sequence[sequence_size++] = !strcmp(tag, "health") ? 'H' : 'I';
    return publish_result;
}
static void fresh(void) {
    free(s_jobs); free(s_done); s_jobs = s_done = NULL;
    s_worker_available = false; s_pending_snap = false;
    s_health_dirty = true; s_next_health = 0; clock_ms = 0;
    wifi_started = mqtt_started = health_sent = images_sent = released = 0;
    sequence_size = 0;
    app_main(); assert(mqtt_started == 1 && camera_calls == 0);
}
static camera_job_t pop_job(void) { camera_job_t job; assert(xQueueReceive(s_jobs, &job, 0)); return job; }
static void finish(camera_job_t job, int err, const uint8_t *frame, size_t len) {
    camera_done_t done = { .job = job, .error = err, .frame = frame, .length = len, .completed_ms = clock_ms };
    xQueueSend(s_done, &done, 0);
}
int main(void) {
    fresh(); report_camera(NULL);
    assert(strstr(last_health, "initializing") && health_sent == 1);
    camera_job_t hung = pop_job();
    clock_ms = 20000; report_camera("health");
    assert(strstr(last_health, "stalled"));
    report_camera(handle_command("snap"));
    assert(strstr(last_health, "\"snap\":\"failed\""));
    unsigned before = health_sent; clock_ms += 300000; report_camera("health");
    assert(health_sent == before + 1 && s_health.attempts == 1 && !s_jobs->full);
    finish(hung, ESP_OK, NULL, 0); report_camera("health");
    assert(s_health.state == CAMERA_STALLED && !s_jobs->full);

    fresh(); report_camera(NULL);
    for (unsigned attempt = 1; attempt <= 3; attempt++) {
        camera_job_t job = pop_job(); assert(job.operation == CAMERA_INIT);
        finish(job, ESP_FAIL, NULL, 0); report_camera("health");
        assert(s_health.attempts == attempt && strstr(last_health, "unavailable"));
        clock_ms += 60000; report_camera("health");
    }
    assert(!s_jobs->full); report_camera("snap");
    assert(strstr(last_health, "\"snap\":\"failed\""));

    fresh(); report_camera(NULL); camera_job_t job = pop_job();
    finish(job, ESP_OK, NULL, 0); report_camera("health");
    job = pop_job(); assert(job.operation == CAMERA_FRAME);
    const uint8_t jpeg[] = {0xff, 0xd8, 0xff};
    finish(job, ESP_OK, jpeg, sizeof(jpeg)); report_camera("health");
    assert(images_sent == 1 && released == 1 && strstr(last_health, "ready"));
    assert(sequence[sequence_size - 1] == 'I');
    before = health_sent; clock_ms += 300000; report_camera("health");
    assert(health_sent == before); /* healthy JPEG stays latest, no JSON overwrite */
    report_camera("snap"); job = pop_job();
    clock_ms += 20000; report_camera("health");
    assert(strstr(last_health, "stalled") && strstr(last_health, "failed"));
    finish(job, ESP_OK, jpeg, sizeof(jpeg)); report_camera("health");
    assert(images_sent == 1 && released == 2); /* no stale success after timeout */

    fresh(); report_camera(NULL); job = pop_job();
    clock_ms = 20000; finish(job, ESP_OK, NULL, 0); report_camera("health");
    assert(s_health.state == CAMERA_STALLED && !s_jobs->full);

    fresh(); report_camera(NULL); job = pop_job();
    clock_ms = 100; finish(job, ESP_OK, NULL, 0);
    clock_ms = 30000; report_camera(NULL); /* completion was on time while network absent */
    assert(s_health.state == CAMERA_READY && s_jobs->full);

    fresh(); report_camera(NULL); job = pop_job();
    finish(job, ESP_OK, NULL, 0); report_camera("health"); job = pop_job();
    finish(job, ESP_OK, jpeg, sizeof(jpeg)); report_camera("health");
    report_camera("snap"); job = pop_job();
    publish_result = CHIRP_CONNECTION_FAILED;
    finish(job, ESP_OK, jpeg, sizeof(jpeg));
    assert(report_camera("health") == CHIRP_CONNECTION_FAILED);
    assert(s_health.snap_failed); /* failed network write never clears failure */
    publish_result = CHIRP_SENT; report_camera(NULL);
    assert(strstr(last_health, "failed"));
    report_camera("heartbeat"); job = pop_job();
    finish(job, ESP_OK, jpeg, sizeof(jpeg)); report_camera("health");
    assert(!s_health.snap_failed && sequence[sequence_size - 1] == 'I');

    fail_worker = true; fresh(); report_camera(NULL);
    assert(mqtt_started == 1 && strstr(last_health, "stalled"));
    publish_result = CHIRP_CONNECTION_FAILED;
    assert(report_camera(NULL) == CHIRP_CONNECTION_FAILED);
    puts("PASS: actual startup/report code: network first, failed/hung init, explicit snap failure, periodic health, JPEG recovery, quarantine, allocation failure, publish failure");
}
