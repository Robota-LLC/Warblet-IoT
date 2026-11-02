/* Host stand-in (test only): log lines are dropped. */
#pragma once
static inline void esp_log_drop(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
#define ESP_LOGD(tag, fmt, ...) esp_log_drop(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) esp_log_drop(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) esp_log_drop(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) esp_log_drop(tag, fmt, ##__VA_ARGS__)
