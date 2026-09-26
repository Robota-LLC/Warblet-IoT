/* Host checks for the CHIRP-PROV push parser in main/chirp_prov.c: a push is
 * applied whole or refused whole, and a present field of the wrong type is
 * bad-json. The two camera demos carry the same parser; point -I at their
 * main/ directory to check them too. From this demo's directory:
 *
 *   gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Itest/stub -Imain \
 *     -I"$IDF_PATH/components/json/cJSON" \
 *     -I"$IDF_PATH/components/mbedtls/mbedtls/include" \
 *     -I"$IDF_PATH/components/mbedtls/mbedtls/library" \
 *     test/provtest.c "$IDF_PATH/components/json/cJSON/cJSON.c" \
 *     "$IDF_PATH/components/mbedtls/mbedtls/library/base64.c" -o provtest
 */

#include <stdio.h>
#include <string.h>

/* strlcpy is not in every host libc. */
#define strlcpy test_strlcpy
static size_t test_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);
    if (size > 0) {
        size_t c = n < size - 1 ? n : size - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}

#include "chirp_prov.c"

static int  fails;
static int  saves;
static int  restarts;

esp_err_t chirp_store_save(const chirp_config_t *config) { (void)config; saves++; return ESP_OK; }
const char *chirp_store_hwid(const chirp_config_t *config) { return config->hwid; }
void esp_restart(void) { restarts++; }
void vTaskDelay(int ticks) { (void)ticks; }
void vTaskDelete(void *task) { (void)task; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, int stack, void *arg, int prio,
                       void *handle)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)handle;
    return pdPASS;
}

/* 32 bytes 0x00..0x1f, base64. An example key: never a device credential. */
#define KEY_B64 "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="

static void expect(const char *json, const char *want, const char *why)
{
    chirp_config_t out;
    const char    *got = parse_push(json, &out);
    const char    *g = got ? got : "ok";
    const char    *w = want ? want : "ok";
    if (strcmp(g, w) != 0) {
        printf("FAIL  %-13s got %-13s %s\n      %s\n", w, g, why, json);
        fails++;
    } else {
        printf("ok    %-13s %s\n", g, why);
    }
}

static void expect_true(int cond, const char *why)
{
    printf("%s  %s\n", cond ? "ok  " : "FAIL", why);
    fails += !cond;
}

int main(void)
{
    chirp_config_t out;
    chirp_config_t stored;

    memset(&s_config, 0, sizeof(s_config));

    puts("--- accepted ---");
    expect("{\"ssid\":\"s\",\"pass\":\"p\",\"token\":\"t\"}", NULL, "one network and a token");
    expect("{\"aps\":[{\"ssid\":\"a\",\"pass\":\"1\"},{\"ssid\":\"b\"}],\"claim\":\"CHIRP-X\","
           "\"key\":\"" KEY_B64 "\",\"level\":2,\"hwid\":\"h\",\"host\":\"example.com\","
           "\"unknown\":{\"x\":[1]}}", NULL, "full push, open second network, unknown key ignored");
    expect("{\"ssid\":\"s\",\"aps\":[],\"token\":\"t\"}", NULL, "empty aps falls back to ssid");

    puts("\n--- refused whole: a present field of the wrong type or shape ---");
    expect("{\"ssid\":\"s\",\"token\":\"t\",\"key\":12345}", "bad-json", "numeric key");
    expect("{\"ssid\":\"s\",\"token\":\"t\",\"key\":\"c2hvcnQ=\"}", "bad-json", "key not 32 bytes");
    expect("{\"ssid\":\"s\",\"token\":\"t\",\"key\":\"!!!!\"}", "bad-json", "key not base64");
    expect("{\"aps\":[{\"ssid\":\"a\"},{\"pass\":\"no ssid\"}],\"token\":\"t\"}", "bad-json",
           "one network without an ssid");
    expect("{\"aps\":[{\"ssid\":\"a\"},{\"ssid\":7}],\"token\":\"t\"}", "bad-json",
           "one network with a numeric ssid");
    expect("{\"aps\":[{\"ssid\":\"a\"},\"b\"],\"token\":\"t\"}", "bad-json", "a network that is not an object");
    expect("{\"aps\":[{\"ssid\":\"a\",\"pass\":null}],\"token\":\"t\"}", "bad-json", "null pass");
    expect("{\"aps\":[{\"ssid\":\"1\"},{\"ssid\":\"2\"},{\"ssid\":\"3\"},{\"ssid\":\"4\"},"
           "{\"ssid\":\"5\"},{\"ssid\":\"6\"},{\"ssid\":\"7\"},{\"ssid\":\"8\"},{\"ssid\":\"9\"}],"
           "\"token\":\"t\"}", "bad-json", "nine networks");
    expect("{\"ssid\":\"s\",\"aps\":\"s\",\"token\":\"t\"}", "bad-json", "aps not a list");
    expect("{\"ssid\":\"s\",\"token\":\"t\",\"host\":5}", "bad-json", "numeric host");
    expect("{\"ssid\":\"s\",\"token\":\"t\",\"hwid\":true}", "bad-json", "boolean hwid");
    expect("{\"ssid\":1,\"token\":\"t\"}", "bad-json", "numeric ssid");
    expect("{\"ssid\":\"s\",\"claim\":\"CHIRP-X\",\"token\":\"t\"}", "bad-json", "claim and token");
    expect("{\"ssid\":\"s\",\"token\":\"t\",\"level\":\"2\"}", "bad-json", "level as a string");
    expect("{\"ssid\":\"s\",\"token\":\"t\",\"level\":1e300}", "bad-json", "level out of range");
    expect("{\"ssid\":\"012345678901234567890123456789012\",\"token\":\"t\"}", "bad-json",
           "33-byte ssid");
    expect("[1]", "bad-json", "not an object");

    puts("\n--- completeness is asked of the merged result ---");
    expect("{\"ssid\":\"s\"}", "missing-field", "no credential stored or pushed");
    expect("{\"token\":\"t\"}", "missing-field", "no network stored or pushed");

    puts("\n--- what a good push leaves behind ---");
    expect_true(parse_push("{\"aps\":[{\"ssid\":\"a\",\"pass\":\"1\"},{\"ssid\":\"b\"}],"
                           "\"claim\":\"CHIRP-X\",\"key\":\"" KEY_B64 "\",\"level\":2}", &out) == NULL &&
                out.ap_count == 2 && strcmp(out.aps[1].ssid, "b") == 0 && out.aps[1].pass[0] == '\0' &&
                out.cred_kind == CHIRP_CRED_CLAIM && out.has_key && out.key[31] == 0x1f &&
                out.has_level && out.level == 2,
                "two networks in order, claim, 32-byte key, level");

    memset(&s_config, 0, sizeof(s_config));
    strcpy(s_config.aps[0].ssid, "home");
    s_config.ap_count  = 1;
    strcpy(s_config.cred, "tok");
    s_config.cred_kind = CHIRP_CRED_TOKEN;
    stored = s_config;
    expect_true(parse_push("{\"key\":\"" KEY_B64 "\"}", &out) == NULL && out.ap_count == 1 &&
                out.cred_kind == CHIRP_CRED_TOKEN && out.has_key,
                "a key-only push keeps the stored network and token");

    puts("\n--- a refused push changes nothing ---");
    saves = restarts = 0;
    handle_push("{\"aps\":[{\"ssid\":\"new\"},{\"ssid\":7}],\"token\":\"other\",\"key\":12345}");
    expect_true(saves == 0 && restarts == 0, "nothing saved, no restart");
    expect_true(memcmp(&stored, &s_config, sizeof(stored)) == 0, "stored config untouched");
    handle_push("{\"ssid\":\"new\",\"token\":\"other\"}");
    expect_true(saves == 1 && restarts == 1, "a good push is saved and restarts");

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
