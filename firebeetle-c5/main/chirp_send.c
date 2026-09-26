#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "chirp_send.h"
#include "chirp_sign.h"
#include "wifi_ladder.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"

static const char *TAG = "chirp_send";

#define REPORT_PERIOD_MS  30000
#define CONNECT_TIMEOUT_S 10

/* Reconnect backoff: immediate after a drop, doubling on failure, reset once a
 * payload line goes out. */
#define BACKOFF_MIN_MS 2000
#define BACKOFF_MAX_MS 60000

/* TCP keepalive is the only way a broken idle connection is noticed. */
#define KEEPALIVE_IDLE_S  30
#define KEEPALIVE_INTVL_S 10
#define KEEPALIVE_CNT     3

/* base64 grows 3 bytes to 4, mbedtls adds a NUL, and the line is "b64:" + that + "\n". */
#define FRAME_MAX   (CHIRP_SIG_SIZE + CHIRP_PAYLOAD_MAX)
#define BASE64_MAX  ((((FRAME_MAX) + 2) / 3) * 4 + 1)
#define TX_LINE_MAX (4 + BASE64_MAX + 1)

#define HELLO_MAX (16 + CHIRP_HWID_SIZE + CHIRP_CRED_SIZE)
#define RX_MAX    512
#define DOWN_MAX  256

static chirp_config_t   s_config;
static chirp_report_fn  s_report;
static chirp_command_fn s_on_command;

/* The send task's socket, or -1. chirp_send_bytes() writes to it from the report callback. */
static int s_socket_fd = -1;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ---------------------------------------------------------------------------
 * the socket
 * ------------------------------------------------------------------------ */

static int tcp_connect(void)
{
    char port[8];
    snprintf(port, sizeof(port), "%u", (unsigned)CHIRP_DEFAULT_PORT);

    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res  = NULL;
    int rc = getaddrinfo(s_config.host, port, &hints, &res);
    if (rc != 0 || res == NULL) {
        ESP_LOGW(TAG, "cannot resolve %s: %d", s_config.host, rc);
        return -1;
    }

    int socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_fd < 0) {
        ESP_LOGW(TAG, "socket: errno %d", errno);
        freeaddrinfo(res);
        return -1;
    }

    struct timeval tv = { .tv_sec = CONNECT_TIMEOUT_S };
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(socket_fd, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGW(TAG, "connect %s:%s: errno %d", s_config.host, port, errno);
        close(socket_fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    int on = 1, idle = KEEPALIVE_IDLE_S, intvl = KEEPALIVE_INTVL_S, cnt = KEEPALIVE_CNT;
    int ka = 0;
    ka |= setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
    ka |= setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    ka |= setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    ka |= setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
    if (ka != 0) {
        ESP_LOGW(TAG, "keepalive rejected by the stack (errno %d): a dropped "
                      "connection will not be noticed", errno);
    }

    ESP_LOGI(TAG, "connected to %s:%s, keepalive %ds/%ds x%d",
             s_config.host, port, KEEPALIVE_IDLE_S, KEEPALIVE_INTVL_S,
             KEEPALIVE_CNT);
    return socket_fd;
}

static void close_socket(void)
{
    if (s_socket_fd >= 0) {
        close(s_socket_fd);
        s_socket_fd = -1;
    }
}

static esp_err_t write_all(const char *buf, size_t len)
{
    while (len > 0) {
        int n = send(s_socket_fd, buf, len, 0);
        if (n <= 0) {
            ESP_LOGW(TAG, "send: errno %d", errno);
            return ESP_FAIL;
        }
        buf += n;
        len -= (size_t)n;
    }
    return ESP_OK;
}

/* Send the device ID and credential in the TCP hello line.
 * The credential is visible on this unencrypted connection. */
static esp_err_t send_hello(void)
{
    char line[HELLO_MAX];
    int  n;

    switch (s_config.cred_kind) {
    case CHIRP_CRED_CLAIM:
        /* A spent claim code becomes the token, so sending this form forever stays correct. */
        n = snprintf(line, sizeof(line), "CHIRP1 %s claim:%s\n",
                     chirp_store_hwid(&s_config), s_config.cred);
        break;
    case CHIRP_CRED_TOKEN:
        n = snprintf(line, sizeof(line), "CHIRP1 %s %s\n",
                     chirp_store_hwid(&s_config), s_config.cred);
        break;
    default:
        /* "-", not empty: the server splits the hello on whitespace. */
        n = snprintf(line, sizeof(line), "CHIRP1 %s -\n",
                     chirp_store_hwid(&s_config));
        break;
    }
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = write_all(line, (size_t)n);
    if (err == ESP_OK) {
        /* Log the hello with the secret field stood in for. */
        ESP_LOGI(TAG, "-> CHIRP1 %s %s", chirp_store_hwid(&s_config),
                 s_config.cred_kind == CHIRP_CRED_CLAIM ? "claim:<code>" :
                 s_config.cred_kind == CHIRP_CRED_TOKEN ? "<token>" : "-");
    }
    return err;
}

/* ---------------------------------------------------------------------------
 * uplink
 * ------------------------------------------------------------------------ */

chirp_send_result_t chirp_send_bytes(const uint8_t *payload, size_t length)
{
    if (payload == NULL || length == 0 || length > CHIRP_PAYLOAD_MAX) {
        ESP_LOGE(TAG, "a payload of %u bytes is not 1..%d - nothing sent",
                 (unsigned)length, CHIRP_PAYLOAD_MAX);
        return CHIRP_SKIPPED;
    }
    if (s_socket_fd < 0) {
        return CHIRP_CONNECTION_FAILED;
    }

    /* Prefix the payload with its 32-byte HMAC-SHA256.
     * This format has no timestamp nonce. */
    uint8_t frame[FRAME_MAX];
    size_t  frame_len;
    if (s_config.has_key) {
        if (chirp_sign_raw(s_config.key, sizeof(s_config.key),
                           payload, length, frame) != ESP_OK) {
            /* A signature that would not compute is not a broken socket. */
            return CHIRP_SKIPPED;
        }
        memcpy(frame + CHIRP_SIG_SIZE, payload, length);
        frame_len = CHIRP_SIG_SIZE + length;
    } else {
        memcpy(frame, payload, length);
        frame_len = length;
    }

    /* Always base64: the payload is binary and a bare line must be UTF-8. */
    char   line[TX_LINE_MAX];
    size_t base64_length = 0;
    memcpy(line, "b64:", 4);
    if (mbedtls_base64_encode((unsigned char *)line + 4, sizeof(line) - 6,
                              &base64_length, frame, frame_len) != 0) {
        return CHIRP_SKIPPED;
    }
    line[4 + base64_length] = '\n';

    /* The log shows the digest, never the key. */
    ESP_LOGI(TAG, "%u bytes, %s -> %.*s", (unsigned)length,
             s_config.has_key ? "signed" : "plain",
             (int)(4 + base64_length), line);

    if (write_all(line, 4 + base64_length + 1) != ESP_OK) {
        return CHIRP_CONNECTION_FAILED;
    }
    return CHIRP_SENT;
}

/* ---------------------------------------------------------------------------
 * downlink
 * ------------------------------------------------------------------------ */

static void handle_down(const char *b64)
{
    unsigned char body[DOWN_MAX];
    size_t        n = 0;

    if (mbedtls_base64_decode(body, sizeof(body) - 1, &n,
                              (const unsigned char *)b64, strlen(b64)) != 0) {
        ESP_LOGW(TAG, "downlink: undecodable base64");
        return;
    }
    body[n] = '\0';

    /* Reserve CHIRPKEY1 for key messages; this demo accepts keys only through USB. */
    if (strncmp((const char *)body, "CHIRPKEY1 ", 10) == 0) {
        ESP_LOGW(TAG, "downlink carries a signing key; this demo collects keys "
                      "over CHIRP-PROV, not over the air");
        return;
    }

    if (s_on_command != NULL) {
        s_on_command((const char *)body);
    }
}

static void handle_line(char *line)
{
    if (strncmp(line, "down:", 5) == 0) {
        handle_down(line + 5);
    } else if (strncmp(line, "err ", 4) == 0) {
        /* Log server errors. Unauthorized closes the connection. */
        ESP_LOGW(TAG, "server: %s", line);
    } else if (line[0] != '\0') {
        ESP_LOGI(TAG, "server: %s", line);
    }
}

/* ---------------------------------------------------------------------------
 * the loop
 * ------------------------------------------------------------------------ */

static void send_task(void *arg)
{
    uint32_t backoff     = BACKOFF_MIN_MS;
    bool     wait_before = false;
    int64_t  next_report = 0;
    char     rx[RX_MAX];
    size_t   rxn = 0;

    for (;;) {
        if (!wifi_ladder_wait_connected(portMAX_DELAY)) {
            continue;
        }

        if (s_socket_fd < 0) {
            if (wait_before) {
                vTaskDelay(pdMS_TO_TICKS(backoff));
            }
            s_socket_fd = tcp_connect();
            if (s_socket_fd >= 0 && send_hello() != ESP_OK) {
                close_socket();
            }
            if (s_socket_fd < 0) {
                wait_before = true;
                backoff = backoff >= BACKOFF_MAX_MS / 2 ? BACKOFF_MAX_MS : backoff * 2;
                continue;
            }
            rxn = 0;
            next_report = 0;  /* report at once on a fresh socket */
        }

        if (now_ms() >= next_report) {
            /* Three results, three cases: a sensor that would not read leaves
             * the socket alone and does not reset the backoff. */
            chirp_send_result_t result = s_report();
            if (result == CHIRP_CONNECTION_FAILED) {
                close_socket();
                wait_before = true;
                continue;
            }
            if (result == CHIRP_SENT) {
                backoff     = BACKOFF_MIN_MS;
                wait_before = false;
            }
            next_report = now_ms() + REPORT_PERIOD_MS;
        }

        /* Wait for commands until the next report is due. */
        int64_t idle_ms = next_report - now_ms();
        if (idle_ms < 0) {
            idle_ms = 0;
        }
        struct timeval tv = {
            .tv_sec  = (time_t)(idle_ms / 1000),
            .tv_usec = (suseconds_t)((idle_ms % 1000) * 1000),
        };
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(s_socket_fd, &readable);
        int ready = select(s_socket_fd + 1, &readable, NULL, NULL, &tv);
        if (ready < 0) {
            close_socket();
            wait_before = true;
            continue;
        }
        if (ready == 0) {
            continue;  /* the next report is due */
        }

        int n = recv(s_socket_fd, rx + rxn, sizeof(rx) - 1 - rxn, 0);
        if (n <= 0) {
            if (n == 0) {
                ESP_LOGW(TAG, "server closed the connection");
            } else {
                ESP_LOGW(TAG, "recv: errno %d", errno);
            }
            close_socket();
            wait_before = true;
            continue;
        }
        rxn += (size_t)n;
        rx[rxn] = '\0';

        char *start = rx;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            size_t len = strlen(start);
            if (len > 0 && start[len - 1] == '\r') {
                start[len - 1] = '\0';
            }
            handle_line(start);
            start = nl + 1;
        }
        /* Keep the partial line; a full buffer with no newline is dropped, not grown. */
        rxn = strlen(start);
        if (rxn >= sizeof(rx) - 1) {
            rxn = 0;
        } else {
            memmove(rx, start, rxn);
        }
    }
}

esp_err_t chirp_send_start(const chirp_config_t *config,
                           chirp_report_fn report,
                           chirp_command_fn on_command)
{
    if (config == NULL || report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config     = *config;
    s_report     = report;
    s_on_command = on_command;

    if (xTaskCreate(send_task, "chirp_send", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
